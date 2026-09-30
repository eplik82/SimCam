// =============================================================================
//  RTSP server – teostus
//
//  Iga klient teenindatakse oma FreeRTOS taskis:
//    1) loeb RTSP päringuid (OPTIONS/DESCRIBE/SETUP/PLAY/PAUSE/TEARDOWN/
//       GET_PARAMETER) ja ignoreerib kliendi RTCP-d (TCP interleaved '$')
//    2) PLAY olekus: võtab uusima JPEG-i, parsib selle ja saadab RTP/JPEG
//       pakettidena (RFC 2435, Q=255 dünaamilised kvantimistabelid).
// =============================================================================
#include "rtsp_server.h"
#include "camera_handler.h"
#include "config.h"
#include "auth.h"
#include "settings.h"
#include "log.h"

#include <Arduino.h>
#include <atomic>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_random.h"

static const char *TAG = "RTSP";

namespace RtspServer {

static std::atomic<int> s_clients{0};

// =============================================================================
//  JPEG parsimine RTP/JPEG jaoks
// =============================================================================
struct JpegInfo {
    const uint8_t *qt[4] = {};
    int      nqt = 0;
    uint8_t  type = 0;        // 0 = 4:2:2, 1 = 4:2:0 (RFC 2435)
    uint16_t width = 0, height = 0;
    uint16_t dri = 0;         // restart interval (0 = puudub)
    const uint8_t *scan = nullptr;
    size_t   scanLen = 0;
};

static bool parseJpeg(const uint8_t *d, size_t n, JpegInfo &j) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 4 <= n) {
        if (d[i] != 0xFF) return false;
        uint8_t m = d[i + 1];
        if (m == 0xFF) { i++; continue; }                     // täitebait
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        size_t len = (d[i + 2] << 8) | d[i + 3];
        if (len < 2 || i + 2 + len > n) return false;
        const uint8_t *seg = d + i + 4;
        size_t segLen = len - 2;

        switch (m) {
        case 0xDB: {                                          // DQT
            size_t k = 0;
            while (k < segLen) {
                uint8_t pq = seg[k] >> 4, tq = seg[k] & 0x0F;
                k++;
                if (pq != 0 || tq > 3 || k + 64 > segLen) return false;  // ainult 8-bit tabelid
                j.qt[tq] = seg + k;
                if (tq + 1 > j.nqt) j.nqt = tq + 1;
                k += 64;
            }
            break;
        }
        case 0xC0: {                                          // SOF0 (baseline)
            if (segLen < 15) return false;
            j.height = (seg[1] << 8) | seg[2];
            j.width  = (seg[3] << 8) | seg[4];
            uint8_t samp = seg[7];                            // komponent 0 (Y) H/V
            if (samp == 0x21) j.type = 0;
            else if (samp == 0x22) j.type = 1;
            else return false;
            break;
        }
        case 0xC1: case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7:
        case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
            return false;                                     // mitte-baseline
        case 0xDD:                                            // DRI
            if (segLen >= 2) j.dri = (seg[0] << 8) | seg[1];
            break;
        case 0xDA: {                                          // SOS → skaneeritud andmed
            size_t start = i + 2 + len;
            size_t end = n;
            // Leia EOI (FF D9) lõpust; esp32-camera puhver võib lõpus sisaldada täidet
            while (end >= start + 2 && !(d[end - 2] == 0xFF && d[end - 1] == 0xD9)) end--;
            if (end < start + 2) end = n; else end -= 2;
            j.scan = d + start;
            j.scanLen = end - start;
            return j.width && j.height && j.nqt > 0 && j.scanLen > 0;
        }
        default: break;
        }
        i += 2 + len;
    }
    return false;
}

// =============================================================================
//  Kliendi sessioon
// =============================================================================
struct Session {
    int      sock = -1;
    int      udp = -1;
    sockaddr_in peer = {};
    bool     tcp = false;
    uint8_t  chRtp = 0, chRtcp = 1;
    uint16_t cliRtp = 0, cliRtcp = 0, srvRtp = 0;
    bool     playing = false;
    bool     setupDone = false;
    uint32_t sessionId = 0;
    uint16_t seq = 0;
    uint32_t ssrc = 0;
    uint32_t lastActivity = 0;
    char     rx[2048];
    size_t   rxLen = 0;
    uint8_t  pkt[4 + 12 + 8 + 4 + 4 + 128 + RTSP_RTP_MAX_PAYLOAD];
    bool     warnedFormat = false;
};

static bool sendAll(int sock, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    while (len) {
        int n = send(sock, p, len, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { vTaskDelay(1); continue; }
            return false;
        }
        p += n;
        len -= n;
    }
    return true;
}

// Saada üks RTP pakett (hdr+payload on juba s.pkt-s alates offsetist 4)
static bool sendRtp(Session &s, size_t rtpLen) {
    if (s.tcp) {
        uint8_t *f = s.pkt;                                   // '$' ch len16
        f[0] = '$';
        f[1] = s.chRtp;
        f[2] = rtpLen >> 8;
        f[3] = rtpLen & 0xFF;
        return sendAll(s.sock, f, rtpLen + 4);
    }
    sockaddr_in to = s.peer;
    to.sin_port = htons(s.cliRtp);
    for (int tries = 0; tries < 50; tries++) {
        int n = sendto(s.udp, s.pkt + 4, rtpLen, 0, (sockaddr *)&to, sizeof(to));
        if (n == (int)rtpLen) return true;
        if (errno != ENOMEM && errno != ENOBUFS && errno != EAGAIN) return false;
        vTaskDelay(pdMS_TO_TICKS(2));                         // lwIP puhvrid täis – oota
    }
    return true;   // UDP: kaotatud pakett pole fataalne
}

// Pakenda JPEG RFC 2435 järgi ja saada
static bool sendJpeg(Session &s, const Camera::Frame &fr) {
    JpegInfo j;
    if (!parseJpeg(fr.buf, fr.len, j)) {
        if (!s.warnedFormat) {
            LOGW(TAG, "JPEG formaati ei saa RTP/JPEG-na saata (vale sampling/SOF?)");
            s.warnedFormat = true;
        }
        return true;   // jäta kaader vahele, ära katkesta ühendust
    }
    if (j.width > 2040 || j.height > 2040) return true;       // RFC 2435 piir

    const uint32_t ts = fr.timestampMs * 90;                  // 90 kHz kell
    const uint8_t *q0 = j.qt[0] ? j.qt[0] : j.qt[1];
    const uint8_t *q1 = j.qt[1] ? j.qt[1] : q0;
    size_t off = 0;

    while (off < j.scanLen) {
        uint8_t *rtp = s.pkt + 4;
        uint8_t *p = rtp + 12;

        // --- JPEG põhipäis (8 B)
        *p++ = 0;                                             // type-specific
        *p++ = (off >> 16) & 0xFF;
        *p++ = (off >> 8) & 0xFF;
        *p++ = off & 0xFF;
        *p++ = j.type + (j.dri ? 64 : 0);
        *p++ = 255;                                           // Q=255 → tabelid kaasas
        *p++ = j.width / 8;
        *p++ = j.height / 8;

        // --- Restart marker päis (DRI korral)
        if (j.dri) {
            *p++ = j.dri >> 8;
            *p++ = j.dri & 0xFF;
            *p++ = 0xFF;                                      // F=1 L=1 count=0x3FFF
            *p++ = 0xFF;
        }

        // --- Kvantimistabelid ainult esimeses fragmendis
        if (off == 0) {
            *p++ = 0;                                         // MBZ
            *p++ = 0;                                         // precision (8-bit)
            *p++ = 0;
            *p++ = 128;                                       // length = 2 × 64
            memcpy(p, q0, 64); p += 64;
            memcpy(p, q1, 64); p += 64;
        }

        size_t hdr = p - rtp;
        size_t room = (12 + RTSP_RTP_MAX_PAYLOAD) > hdr ? (12 + RTSP_RTP_MAX_PAYLOAD) - hdr : 0;
        size_t chunk = min(room, j.scanLen - off);
        memcpy(p, j.scan + off, chunk);
        bool last = (off + chunk >= j.scanLen);

        // --- RTP päis (12 B)
        rtp[0] = 0x80;                                        // V=2
        rtp[1] = (last ? 0x80 : 0x00) | 26;                   // M-bitt + PT 26 (JPEG)
        rtp[2] = s.seq >> 8;
        rtp[3] = s.seq & 0xFF;
        s.seq++;
        rtp[4] = ts >> 24; rtp[5] = ts >> 16; rtp[6] = ts >> 8; rtp[7] = ts;
        rtp[8] = s.ssrc >> 24; rtp[9] = s.ssrc >> 16; rtp[10] = s.ssrc >> 8; rtp[11] = s.ssrc;

        if (!sendRtp(s, hdr + chunk)) return false;
        off += chunk;
    }
    return true;
}

// =============================================================================
//  RTSP päringute töötlemine
// =============================================================================
// Leia päise väärtus (tõstutundetu). Tagastab false, kui puudub.
static bool header(const char *req, const char *name, char *out, size_t outLen) {
    size_t nl = strlen(name);
    const char *p = strstr(req, "\r\n");
    while (p) {
        p += 2;
        if (*p == '\r') break;                                // päiste lõpp
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            const char *v = p + nl + 1;
            while (*v == ' ') v++;
            const char *e = strstr(v, "\r\n");
            size_t l = e ? (size_t)(e - v) : strlen(v);
            if (l >= outLen) l = outLen - 1;
            memcpy(out, v, l);
            out[l] = 0;
            return true;
        }
        p = strstr(p, "\r\n");
    }
    return false;
}

// Tee URL-ist tee (rtsp://host:554/live/track1 → /live/track1)
static const char *urlPath(const char *url) {
    const char *p = strstr(url, "://");
    if (!p) return url;
    p = strchr(p + 3, '/');
    return p ? p : "/";
}

static void reply(Session &s, int code, const char *reason, const char *cseq,
                  const char *extra = "", const char *body = nullptr) {
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
                     "RTSP/1.0 %d %s\r\nCSeq: %s\r\nServer: SimCam\r\n%s",
                     code, reason, cseq, extra);
    if (body) n += snprintf(buf + n, sizeof(buf) - n, "Content-Length: %u\r\n\r\n%s",
                            (unsigned)strlen(body), body);
    else n += snprintf(buf + n, sizeof(buf) - n, "\r\n");
    sendAll(s.sock, buf, n);
}

static bool openUdp(Session &s) {
    s.udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s.udp < 0) return false;
    // Proovi paarisarvulist porti vahemikus 6970–6999 (RTP), RTCP = +1
    for (uint16_t port = 6970; port < 7000; port += 2) {
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(port);
        if (bind(s.udp, (sockaddr *)&a, sizeof(a)) == 0) { s.srvRtp = port; return true; }
    }
    close(s.udp);
    s.udp = -1;
    return false;
}

// Tagastab false, kui ühendus tuleb sulgeda
static bool handleRequest(Session &s, char *req) {
    char method[16] = "", url[160] = "", cseq[16] = "0";
    sscanf(req, "%15s %159s", method, url);
    header(req, "CSeq", cseq, sizeof(cseq));
    s.lastActivity = millis();
    LOGI(TAG, "%s %s", method, url);

    // --- Autentimine (kui AUTH_PASS on määratud)
    if (Auth::enabled() && Settings::get().rtspAuth && strcmp(method, "OPTIONS") != 0) {
        char auth[128] = "";
        header(req, "Authorization", auth, sizeof(auth));
        if (!Auth::check(auth)) {
            reply(s, 401, "Unauthorized", cseq, "WWW-Authenticate: Basic realm=\"SimCam\"\r\n");
            return true;
        }
    }

    const char *path = urlPath(url);
    bool pathOk = strncmp(path, RTSP_PATH, strlen(RTSP_PATH)) == 0 || strcmp(url, "*") == 0;

    if (!strcmp(method, "OPTIONS")) {
        reply(s, 200, "OK", cseq,
              "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER\r\n");
    } else if (!strcmp(method, "DESCRIBE")) {
        if (!pathOk) { reply(s, 404, "Not Found", cseq); return true; }
        sockaddr_in me = {};
        socklen_t ml = sizeof(me);
        getsockname(s.sock, (sockaddr *)&me, &ml);
        char ip[16];
        inet_ntoa_r(me.sin_addr, ip, sizeof(ip));
        char sdp[400];
        snprintf(sdp, sizeof(sdp),
                 "v=0\r\n"
                 "o=- %lu 1 IN IP4 %s\r\n"
                 "s=SimCam live\r\n"
                 "c=IN IP4 0.0.0.0\r\n"
                 "t=0 0\r\n"
                 "a=control:*\r\n"
                 "a=range:npt=0-\r\n"
                 "m=video 0 RTP/AVP 26\r\n"
                 "a=rtpmap:26 JPEG/90000\r\n"
                 "a=framerate:%d\r\n"
                 "a=control:track1\r\n",
                 (unsigned long)s.sessionId, ip, CAM_MAX_FPS);
        char extra[256];
        // Content-Base lõpeb '/'-ga, et track1 lahenduks /live/track1-ks
        const char *slash = url[strlen(url) - 1] == '/' ? "" : "/";
        snprintf(extra, sizeof(extra),
                 "Content-Base: %s%s\r\nContent-Type: application/sdp\r\n", url, slash);
        reply(s, 200, "OK", cseq, extra, sdp);
    } else if (!strcmp(method, "SETUP")) {
        if (!pathOk) { reply(s, 404, "Not Found", cseq); return true; }
        char tr[160] = "";
        header(req, "Transport", tr, sizeof(tr));
        char extra[256];
        if (strstr(tr, "RTP/AVP/TCP")) {
            s.tcp = true;
            int a = 0, b = 1;
            const char *il = strstr(tr, "interleaved=");
            if (il) sscanf(il, "interleaved=%d-%d", &a, &b);
            s.chRtp = a; s.chRtcp = b;
            snprintf(extra, sizeof(extra),
                     "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;ssrc=%08lX\r\n"
                     "Session: %08lX;timeout=%d\r\n",
                     a, b, (unsigned long)s.ssrc, (unsigned long)s.sessionId, RTSP_SESSION_TIMEOUT);
        } else {
            const char *cp = strstr(tr, "client_port=");
            int a = 0, b = 0;
            if (!cp || sscanf(cp, "client_port=%d-%d", &a, &b) < 1 || (s.udp < 0 && !openUdp(s))) {
                reply(s, 461, "Unsupported Transport", cseq);
                return true;
            }
            s.tcp = false;
            s.cliRtp = a; s.cliRtcp = b ? b : a + 1;
            snprintf(extra, sizeof(extra),
                     "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d;ssrc=%08lX\r\n"
                     "Session: %08lX;timeout=%d\r\n",
                     s.cliRtp, s.cliRtcp, s.srvRtp, s.srvRtp + 1, (unsigned long)s.ssrc,
                     (unsigned long)s.sessionId, RTSP_SESSION_TIMEOUT);
        }
        s.setupDone = true;
        reply(s, 200, "OK", cseq, extra);
    } else if (!strcmp(method, "PLAY")) {
        if (!s.setupDone) { reply(s, 455, "Method Not Valid in This State", cseq); return true; }
        char extra[256];
        snprintf(extra, sizeof(extra),
                 "Session: %08lX\r\nRange: npt=0.000-\r\nRTP-Info: url=%s;seq=%u;rtptime=%lu\r\n",
                 (unsigned long)s.sessionId, url, s.seq, (unsigned long)(millis() * 90));
        reply(s, 200, "OK", cseq, extra);
        if (!s.playing) { s.playing = true; Camera::addConsumer(); }
        LOGI(TAG, "Voog käivitatud (%s)", s.tcp ? "TCP" : "UDP");
    } else if (!strcmp(method, "PAUSE")) {
        if (s.playing) { s.playing = false; Camera::removeConsumer(); }
        char extra[48];
        snprintf(extra, sizeof(extra), "Session: %08lX\r\n", (unsigned long)s.sessionId);
        reply(s, 200, "OK", cseq, extra);
    } else if (!strcmp(method, "TEARDOWN")) {
        char extra[48];
        snprintf(extra, sizeof(extra), "Session: %08lX\r\n", (unsigned long)s.sessionId);
        reply(s, 200, "OK", cseq, extra);
        return false;
    } else if (!strcmp(method, "GET_PARAMETER") || !strcmp(method, "SET_PARAMETER")) {
        char extra[48];
        snprintf(extra, sizeof(extra), "Session: %08lX\r\n", (unsigned long)s.sessionId);
        reply(s, 200, "OK", cseq, extra);    // keep-alive
    } else {
        reply(s, 501, "Not Implemented", cseq);
    }
    return true;
}

// Töötle vastuvõetud baite: RTSP päringud + interleaved RTCP ('$')
static bool processInput(Session &s) {
    for (;;) {
        if (s.rxLen == 0) return true;
        if (s.rx[0] == '$') {                                 // kliendi RTCP (TCP)
            if (s.rxLen < 4) return true;
            size_t l = 4 + (((uint8_t)s.rx[2] << 8) | (uint8_t)s.rx[3]);
            if (s.rxLen < l) {
                if (l > sizeof(s.rx)) return false;
                return true;
            }
            memmove(s.rx, s.rx + l, s.rxLen - l);
            s.rxLen -= l;
            s.lastActivity = millis();
            continue;
        }
        s.rx[s.rxLen] = 0;
        char *end = strstr(s.rx, "\r\n\r\n");
        if (!end) return s.rxLen < sizeof(s.rx) - 1;          // oota ülejäänud päist
        size_t hdrLen = end + 4 - s.rx;
        end[2] = 0;                                           // päis lõpeb "\r\n" + NUL
        char cl[12] = "0";
        header(s.rx, "Content-Length", cl, sizeof(cl));
        size_t total = hdrLen + atoi(cl);                     // keha (kui on) jäetakse vahele
        if (total > s.rxLen) { end[2] = '\r'; return total < sizeof(s.rx); }
        bool keep = handleRequest(s, s.rx);
        memmove(s.rx, s.rx + total, s.rxLen - total);
        s.rxLen -= total;
        if (!keep) return false;
    }
}

// =============================================================================
//  Kliendi task
// =============================================================================
static void clientTask(void *arg) {
    Session *s = (Session *)arg;
    Camera::Frame fr;
    uint32_t lastSeq = 0;

    int one = 1;
    setsockopt(s->sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    timeval sndTo = {10, 0};                                  // aeglane mobiilivõrk
    setsockopt(s->sock, SOL_SOCKET, SO_SNDTIMEO, &sndTo, sizeof(sndTo));

    char ip[16];
    inet_ntoa_r(s->peer.sin_addr, ip, sizeof(ip));
    LOGI(TAG, "Klient ühendus: %s (%d/%d)", ip, (int)s_clients, RTSP_MAX_CLIENTS);

    for (;;) {
        // 1) Sissetulevad päringud (mitteblokeeriv, kui voog käib)
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s->sock, &rd);
        timeval tv = {0, s->playing ? 5000 : 500000};
        int r = select(s->sock + 1, &rd, nullptr, nullptr, &tv);
        if (r < 0) break;
        if (r > 0) {
            int n = recv(s->sock, s->rx + s->rxLen, sizeof(s->rx) - 1 - s->rxLen, 0);
            if (n <= 0) break;
            s->rxLen += n;
            if (!processInput(*s)) break;
        }

        // 2) Session timeout (UDP klient peab saatma keep-alive'i)
        if (!s->tcp && millis() - s->lastActivity > RTSP_SESSION_TIMEOUT * 1000UL) {
            LOGW(TAG, "Session timeout");
            break;
        }

        // 3) Uus kaader → saada
        if (s->playing && Camera::waitFrame(fr, lastSeq, 0)) {
            lastSeq = fr.seq;
            if (!sendJpeg(*s, fr)) { LOGW(TAG, "Saatmine ebaõnnestus – sulgen"); break; }
        }
    }

    if (s->playing) Camera::removeConsumer();
    if (s->udp >= 0) close(s->udp);
    shutdown(s->sock, SHUT_RDWR);
    close(s->sock);
    Camera::freeFrame(fr);
    LOGI(TAG, "Klient lahkus: %s", ip);
    delete s;
    s_clients--;
    vTaskDelete(nullptr);
}

// =============================================================================
//  Kuulav task
// =============================================================================
static void serverTask(void *) {
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(RTSP_PORT);
    if (bind(ls, (sockaddr *)&a, sizeof(a)) != 0 || listen(ls, 4) != 0) {
        LOGE(TAG, "Port %d kuulamine ebaõnnestus (errno %d)", RTSP_PORT, errno);
        close(ls);
        vTaskDelete(nullptr);
        return;
    }
    LOGI(TAG, "RTSP server kuulab pordil %d, tee %s", RTSP_PORT, RTSP_PATH);

    for (;;) {
        sockaddr_in peer = {};
        socklen_t pl = sizeof(peer);
        int cs = accept(ls, (sockaddr *)&peer, &pl);
        if (cs < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        if (s_clients >= RTSP_MAX_CLIENTS) {
            // Loe päring, et vastata õige CSeq-ga – muidu tõlgendab VLC seda
            // ühenduse timeout'ina ja kukub tagasi teise mooduli peale.
            timeval to = {2, 0};
            setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
            char req[512];
            int n = recv(cs, req, sizeof(req) - 1, 0);
            char cseq[16] = "0";
            if (n > 0) { req[n] = 0; header(req, "CSeq", cseq, sizeof(cseq)); }
            char busy[128];
            int bl = snprintf(busy, sizeof(busy),
                              "RTSP/1.0 503 Service Unavailable\r\nCSeq: %s\r\nServer: SimCam\r\n\r\n", cseq);
            send(cs, busy, bl, 0);
            close(cs);
            LOGW(TAG, "Liiga palju kliente (%d) – keeldun", RTSP_MAX_CLIENTS);
            continue;
        }

        Session *s = new (std::nothrow) Session();
        if (!s) { close(cs); continue; }
        s->sock = cs;
        s->peer = peer;
        s->sessionId = esp_random();
        s->ssrc = esp_random();
        s->seq = esp_random() & 0xFFFF;
        s->lastActivity = millis();
        s_clients++;
        if (xTaskCreatePinnedToCore(clientTask, "rtsp_cli", 8192, s, 3, nullptr, 1) != pdPASS) {
            close(cs);
            delete s;
            s_clients--;
        }
    }
}

bool begin() {
    return xTaskCreatePinnedToCore(serverTask, "rtsp_srv", 4096, nullptr, 3, nullptr, 1) == pdPASS;
}

int clients() { return s_clients; }

}  // namespace RtspServer
