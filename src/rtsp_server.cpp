// =============================================================================
//  RTSP server – teostus
//
//  Iga klient teenindatakse oma FreeRTOS taskis:
//    1) loeb RTSP päringuid (OPTIONS/DESCRIBE/SETUP/PLAY/PAUSE/TEARDOWN/
//       GET_PARAMETER) ja ignoreerib kliendi RTCP-d (TCP interleaved '$')
//    2) PLAY olekus: võtab uusima JPEG-i, parsib selle ja saadab RTP/JPEG
//       pakettidena (RFC 2435, Q=255 dünaamilised kvantimistabelid).
//    3) Kui mikrofon on sees: helirada (track2) – G.711 µ-law 8 kHz (PT 0)
//       või L16 16 kHz (PT 97), 20 ms paketid. Mõlemale rajale saadetakse
//       iga 5 s järel RTCP Sender Report, et mängija saaks pildi ja heli
//       ühisele ajateljele panna (mõlema RTP aeg tuleb millis()-ist).
// =============================================================================
#include "rtsp_server.h"
#include "camera_handler.h"
#include "config.h"
#include "auth.h"
#include "settings.h"
#include "audio.h"
#include "log.h"

#include <Arduino.h>
#include <atomic>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_random.h"
#include "mbedtls/md5.h"
#include "mbedtls/base64.h"
#include "version.h"

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
struct Track {
    bool     setup = false;
    uint8_t  chRtp = 0, chRtcp = 1;          // TCP interleaved kanalid
    uint16_t cliRtp = 0, cliRtcp = 0;        // UDP kliendi pordid
    uint16_t seq = 0;
    uint32_t ssrc = 0;
    uint32_t pkts = 0, octets = 0;           // RTCP SR jaoks
};

#define AUDIO_PKT_SAMPLES  (MIC_RATE / 50)   // 20 ms 16 kHz diskreete
#define AUDIO_MAX_LAG_MS   400               // rohkem maha jäänud heli jäetakse vahele

struct Session {
    int      sock = -1;
    int      udp = -1;                        // üks UDP sokkel mõlemale rajale
    sockaddr_in peer = {};
    bool     tcp = false;
    uint16_t srvRtp = 0;
    Track    v, a;                            // pilt (track1), heli (track2)
    bool     audio = false;                   // heli pakuti SDP-s
    uint8_t  aCodec = 0;                      // 0 = PCMU 8 kHz, 1 = L16 16 kHz
    uint32_t aPos = 0, aTs = 0;               // heli lugemiskoht ja järgmise paketi RTP aeg
    uint32_t lastSr = 0;
    bool     playing = false;
    uint32_t sessionId = 0;
    uint32_t lastActivity = 0;
    char     rx[2048];
    size_t   rxLen = 0;
    uint8_t  pkt[4 + 12 + 8 + 4 + 4 + 128 + RTSP_RTP_MAX_PAYLOAD];
    uint8_t  apkt[4 + 12 + AUDIO_PKT_SAMPLES * 2];
    bool     warnedFormat = false;
};

static inline void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

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

// Saada üks RTP/RTCP pakett (andmed on f-is alates offsetist 4; esimesed 4 baiti
// on TCP interleaved päise jaoks)
static bool sendPkt(Session &s, uint8_t ch, uint16_t port, uint8_t *f, size_t rtpLen) {
    if (s.tcp) {
        f[0] = '$';                                           // '$' ch len16
        f[1] = ch;
        f[2] = rtpLen >> 8;
        f[3] = rtpLen & 0xFF;
        return sendAll(s.sock, f, rtpLen + 4);
    }
    sockaddr_in to = s.peer;
    to.sin_port = htons(port);
    for (int tries = 0; tries < 50; tries++) {
        int n = sendto(s.udp, f + 4, rtpLen, 0, (sockaddr *)&to, sizeof(to));
        if (n == (int)rtpLen) return true;
        if (errno != ENOMEM && errno != ENOBUFS && errno != EAGAIN) return false;
        vTaskDelay(pdMS_TO_TICKS(2));                         // lwIP puhvrid täis – oota
    }
    return true;   // UDP: kaotatud pakett pole fataalne
}

static bool sendRtp(Session &s, Track &t, uint8_t *f, size_t rtpLen) {
    t.pkts++;
    t.octets += rtpLen - 12;
    return sendPkt(s, t.chRtp, t.cliRtp, f, rtpLen);
}

// RTCP Sender Report: seob raja RTP aja seinakellaga (NTP), et mängija saaks
// pildi ja heli sünkroniseerida. Seinakellaks on millis() (ühine mõlemale rajale).
static void sendSr(Session &s, Track &t, uint32_t rtpNow, uint32_t ms) {
    uint8_t b[4 + 28];
    uint8_t *r = b + 4;
    r[0] = 0x80; r[1] = 200; r[2] = 0; r[3] = 6;              // V=2, PT=SR, length=6
    put32(r + 4, t.ssrc);
    put32(r + 8, 2208988800UL + ms / 1000);                   // NTP sekundid (1900-st)
    put32(r + 12, (uint32_t)(((uint64_t)(ms % 1000) << 32) / 1000));
    put32(r + 16, rtpNow);
    put32(r + 20, t.pkts);
    put32(r + 24, t.octets);
    sendPkt(s, t.chRtcp, t.cliRtcp, b, 28);
}

static uint32_t audioRate(const Session &s) { return s.aCodec ? 16000 : 8000; }

// Saada kogu saadaolev heli 20 ms pakettidena
static bool sendAudio(Session &s) {
    int16_t pcm[AUDIO_PKT_SAMPLES];
    uint32_t skipped;
    const uint32_t rate = audioRate(s);
    while (Audio::read(&s.aPos, pcm, AUDIO_PKT_SAMPLES, AUDIO_MAX_LAG_MS, &skipped)) {
        if (skipped) s.aTs += (uint64_t)skipped * rate / MIC_RATE;   // vahelejäetud aeg
        uint8_t *rtp = s.apkt + 4;
        uint8_t *p = rtp + 12;
        size_t len;
        if (s.aCodec) {                                       // L16: big-endian
            for (int i = 0; i < AUDIO_PKT_SAMPLES; i++) { p[2 * i] = pcm[i] >> 8; p[2 * i + 1] = pcm[i] & 0xFF; }
            len = AUDIO_PKT_SAMPLES * 2;
        } else {
            len = Audio::encodePcmu(pcm, AUDIO_PKT_SAMPLES, p);
        }
        rtp[0] = 0x80;
        rtp[1] = s.aCodec ? 97 : 0;
        rtp[2] = s.a.seq >> 8; rtp[3] = s.a.seq & 0xFF;
        s.a.seq++;
        put32(rtp + 4, s.aTs);
        put32(rtp + 8, s.a.ssrc);
        if (!sendRtp(s, s.a, s.apkt, 12 + len)) return false;
        s.aTs += (uint64_t)AUDIO_PKT_SAMPLES * rate / MIC_RATE;
    }
    return true;
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
        rtp[2] = s.v.seq >> 8;
        rtp[3] = s.v.seq & 0xFF;
        s.v.seq++;
        put32(rtp + 4, ts);
        put32(rtp + 8, s.v.ssrc);

        if (!sendRtp(s, s.v, s.pkt, hdr + chunk)) return false;
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
            // Vale parool → selge hoiatus logis (VMS proovib iga sekund – kuni kord minutis)
            static uint32_t lastWarn = 0;
            if (auth[0] && (!lastWarn || millis() - lastWarn > 60000)) {
                lastWarn = millis();
                char user[48] = "?";
                Auth::basicUser(auth, user, sizeof(user));
                char ip[16];
                inet_ntoa_r(s.peer.sin_addr, ip, sizeof(ip));
                LOGW(TAG, "Vale RTSP kasutaja/parool kliendilt %s (kasutaja '%s') – kontrolli kaamera seadeid "
                          "salvestis/VMS-is: aadress rtsp://<IP>:554/live, kasutaja admin, parool = veebiliidese parool",
                     ip, user);
            }
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
        const Settings::Data cfg = Settings::get();
        s.audio = Audio::enabled() && cfg.rtspAudio;   // mikrofon käivitub PLAY-ga koos kaameraga
        s.aCodec = cfg.micCodec ? 1 : 0;
        char sdp[640];
        int sl = snprintf(sdp, sizeof(sdp),
                 "v=0\r\n"
                 "o=- %lu 1 IN IP4 %s\r\n"
                 "s=%s\r\n"
                 "c=IN IP4 0.0.0.0\r\n"
                 "t=0 0\r\n"
                 "a=control:*\r\n"
                 "a=range:npt=0-\r\n"
                 "m=video 0 RTP/AVP 26\r\n"
                 "a=rtpmap:26 JPEG/90000\r\n"
                 "a=framerate:%d\r\n"
                 "a=control:track1\r\n",
                 (unsigned long)s.sessionId, ip, cfg.camName, CAM_MAX_FPS);
        if (s.audio)
            snprintf(sdp + sl, sizeof(sdp) - sl, s.aCodec ?
                     "m=audio 0 RTP/AVP 97\r\na=rtpmap:97 L16/16000/1\r\na=control:track2\r\n" :
                     "m=audio 0 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\na=control:track2\r\n");
        char extra[256];
        // Content-Base lõpeb '/'-ga, et track1 lahenduks /live/track1-ks
        const char *slash = url[strlen(url) - 1] == '/' ? "" : "/";
        snprintf(extra, sizeof(extra),
                 "Content-Base: %s%s\r\nContent-Type: application/sdp\r\n", url, slash);
        reply(s, 200, "OK", cseq, extra, sdp);
    } else if (!strcmp(method, "SETUP")) {
        if (!pathOk) { reply(s, 404, "Not Found", cseq); return true; }
        const bool isAudio = strstr(path, "track2") != nullptr;
        if (isAudio && !s.audio) { reply(s, 404, "Not Found", cseq); return true; }
        Track &t = isAudio ? s.a : s.v;
        char tr[160] = "";
        header(req, "Transport", tr, sizeof(tr));
        char extra[256];
        if (strstr(tr, "RTP/AVP/TCP")) {
            s.tcp = true;
            int a = isAudio ? 2 : 0, b = a + 1;
            const char *il = strstr(tr, "interleaved=");
            if (il) sscanf(il, "interleaved=%d-%d", &a, &b);
            t.chRtp = a; t.chRtcp = b;
            snprintf(extra, sizeof(extra),
                     "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;ssrc=%08lX\r\n"
                     "Session: %08lX;timeout=%d\r\n",
                     a, b, (unsigned long)t.ssrc, (unsigned long)s.sessionId, RTSP_SESSION_TIMEOUT);
        } else {
            const char *cp = strstr(tr, "client_port=");
            int a = 0, b = 0;
            if (!cp || sscanf(cp, "client_port=%d-%d", &a, &b) < 1 || (s.udp < 0 && !openUdp(s))) {
                reply(s, 461, "Unsupported Transport", cseq);
                return true;
            }
            s.tcp = false;
            t.cliRtp = a; t.cliRtcp = b ? b : a + 1;
            snprintf(extra, sizeof(extra),
                     "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d;ssrc=%08lX\r\n"
                     "Session: %08lX;timeout=%d\r\n",
                     t.cliRtp, t.cliRtcp, s.srvRtp, s.srvRtp + 1, (unsigned long)t.ssrc,
                     (unsigned long)s.sessionId, RTSP_SESSION_TIMEOUT);
        }
        t.setup = true;
        reply(s, 200, "OK", cseq, extra);
    } else if (!strcmp(method, "PLAY")) {
        if (!s.v.setup && !s.a.setup) { reply(s, 455, "Method Not Valid in This State", cseq); return true; }
        const uint32_t now = millis();
        if (s.a.setup) {                                      // heli alates uusimast diskreedist
            s.aPos = Audio::position();
            s.aTs = (uint32_t)((uint64_t)Audio::msAt(s.aPos) * audioRate(s) / 1000);
        }
        // RTP-Info: iga raja URL, järjekorranumber ja RTP aeg
        char base[160];
        strlcpy(base, url, sizeof(base));
        size_t bl = strlen(base);
        if (bl && base[bl - 1] == '/') base[--bl] = 0;
        char *tk = strstr(base, "/track");                    // mõni klient saadab PLAY raja URL-iga
        if (tk) *tk = 0;
        char info[400];
        int il = 0;
        if (s.v.setup)
            il += snprintf(info + il, sizeof(info) - il, "url=%s/track1;seq=%u;rtptime=%lu",
                           base, s.v.seq, (unsigned long)(now * 90));
        if (s.a.setup)
            il += snprintf(info + il, sizeof(info) - il, "%surl=%s/track2;seq=%u;rtptime=%lu",
                           il ? "," : "", base, s.a.seq, (unsigned long)s.aTs);
        char extra[512];
        snprintf(extra, sizeof(extra), "Session: %08lX\r\nRange: npt=0.000-\r\nRTP-Info: %s\r\n",
                 (unsigned long)s.sessionId, info);
        reply(s, 200, "OK", cseq, extra);
        if (!s.playing) {
            s.playing = true;
            if (s.v.setup) Camera::addConsumer();
            s.lastSr = 0;
        }
        LOGI(TAG, "Voog käivitatud (%s%s%s)", s.tcp ? "TCP" : "UDP", s.v.setup ? ", pilt" : "",
             s.a.setup ? (s.aCodec ? ", heli L16 16 kHz" : ", heli G.711 8 kHz") : "");
    } else if (!strcmp(method, "PAUSE")) {
        if (s.playing) { s.playing = false; if (s.v.setup) Camera::removeConsumer(); }
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
        if (s->playing && s->v.setup && Camera::waitFrame(fr, lastSeq, 0)) {
            lastSeq = fr.seq;
            if (!sendJpeg(*s, fr)) { LOGW(TAG, "Saatmine ebaõnnestus – sulgen"); break; }
        }

        // 4) Heli (20 ms paketid)
        if (s->playing && s->a.setup && !sendAudio(*s)) { LOGW(TAG, "Heli saatmine ebaõnnestus – sulgen"); break; }

        // 5) RTCP Sender Report iga 5 s järel (pildi ja heli sünkroniseerimiseks)
        if (s->playing && millis() - s->lastSr >= 5000) {
            const uint32_t now = millis();
            s->lastSr = now;
            if (s->v.setup) sendSr(*s, s->v, now * 90, now);
            if (s->a.setup)
                sendSr(*s, s->a, s->aTs + (int32_t)(now - Audio::msAt(s->aPos)) * (int32_t)audioRate(*s) / 1000, now);
        }
    }

    if (s->playing && s->v.setup) Camera::removeConsumer();
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
        s->v.ssrc = esp_random();
        s->v.seq = esp_random() & 0xFFFF;
        s->a.ssrc = esp_random();
        s->a.seq = esp_random() & 0xFFFF;
        s->lastActivity = millis();
        s_clients++;
        if (xTaskCreatePinnedToCore(clientTask, "rtsp_cli", 8192, s, 3, nullptr, 1) != pdPASS) {
            close(cs);
            delete s;
            s_clients--;
        }
    }
}

// =============================================================================
//  RTSP push (ANNOUNCE + RECORD) – seade saadab voo ise serverisse, nt MediaMTX
// =============================================================================
//  URL: rtsp://[kasutaja:parool@]host[:port]/tee   (vaikeport 554)
//  Kõik üle ühe TCP ühenduse (RTP/AVP/TCP interleaved) → töötab NAT/CGNAT taga.
//  Autentimine: Basic või Digest (MD5), vastavalt serveri 401 vastusele.
static PushStatus s_push;
static portMUX_TYPE s_pushMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_pushKick = false;

static void pushState(const char *st, const char *err = nullptr) {
    portENTER_CRITICAL(&s_pushMux);
    strlcpy(s_push.state, st, sizeof(s_push.state));
    if (err) strlcpy(s_push.error, err, sizeof(s_push.error));
    portEXIT_CRITICAL(&s_pushMux);
}

struct PushUrl {
    char host[96] = "", path[128] = "/", user[48] = "", pass[64] = "";
    uint16_t port = 554;
    char clean[200] = "";                       // URL ilma kasutaja ja paroolita (päringutes)
};

static void urlUnescape(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((uint8_t)s[1]) && isxdigit((uint8_t)s[2])) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, nullptr, 16);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

static bool parsePushUrl(const char *url, PushUrl &u) {
    if (strncasecmp(url, "rtsp://", 7)) return false;
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    const char *hostEnd = slash ? slash : p + strlen(p);
    const char *at = nullptr;
    for (const char *q = p; q < hostEnd; q++) if (*q == '@') at = q;   // viimane @ enne teed
    if (at) {
        const char *colon = (const char *)memchr(p, ':', at - p);
        size_t ul = (colon ? colon : at) - p;
        if (ul >= sizeof(u.user)) return false;
        memcpy(u.user, p, ul); u.user[ul] = 0;
        if (colon) {
            size_t pl = at - colon - 1;
            if (pl >= sizeof(u.pass)) return false;
            memcpy(u.pass, colon + 1, pl); u.pass[pl] = 0;
        }
        urlUnescape(u.user); urlUnescape(u.pass);
        p = at + 1;
    }
    const char *colon = (const char *)memchr(p, ':', hostEnd - p);
    size_t hl = (colon ? colon : hostEnd) - p;
    if (!hl || hl >= sizeof(u.host)) return false;
    memcpy(u.host, p, hl); u.host[hl] = 0;
    if (colon) {
        int port = atoi(colon + 1);
        if (port <= 0 || port > 65535) return false;
        u.port = port;
    }
    strlcpy(u.path, slash ? slash : "/", sizeof(u.path));
    snprintf(u.clean, sizeof(u.clean), "rtsp://%s:%u%s", u.host, u.port, u.path);
    return true;
}

static void md5hex(const char *in, char out[33]) {
    uint8_t d[16];
    mbedtls_md5((const unsigned char *)in, strlen(in), d);
    for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

// Loe ühe RTSP vastuse päis + keha; vahepealsed '$' interleaved paketid jäetakse vahele
static int readResponse(int sock, char *buf, size_t cap, uint32_t timeoutMs) {
    size_t len = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        // interleaved pakett vastuse alguses → viska ära
        while (len >= 4 && buf[0] == '$') {
            size_t pl = 4 + (((uint8_t)buf[2] << 8) | (uint8_t)buf[3]);
            if (len < pl) break;
            memmove(buf, buf + pl, len - pl);
            len -= pl;
        }
        buf[len] = 0;
        char *end = strstr(buf, "\r\n\r\n");
        if (end && buf[0] != '$') {
            size_t hdr = end + 4 - buf, body = 0;
            char cl[12];
            if (header(buf, "Content-Length", cl, sizeof(cl))) body = atoi(cl);
            if (len >= hdr + body) {
                int code = 0;
                sscanf(buf, "RTSP/%*s %d", &code);
                return code;
            }
        }
        if (len + 1 >= cap) return -2;
        fd_set rd; FD_ZERO(&rd); FD_SET(sock, &rd);
        timeval tv = {0, 200000};
        int r = select(sock + 1, &rd, nullptr, nullptr, &tv);
        if (r < 0) return -1;
        if (r == 0) continue;
        int n = recv(sock, buf + len, cap - 1 - len, 0);
        if (n <= 0) return -1;
        len += n;
    }
    return -3;   // ajalõpp
}

struct PushAuth {
    bool digest = false, basic = false;
    char realm[96] = "", nonce[128] = "", opaque[96] = "";
};

static void parseAuth(const char *resp, PushAuth &a) {
    // võib olla mitu WWW-Authenticate päist – eelista Digest-it
    const char *p = resp;
    while ((p = strcasestr(p, "\nWWW-Authenticate:"))) {
        p += 18;
        while (*p == ' ') p++;
        auto val = [&](const char *key, char *out, size_t n) {
            const char *k = strcasestr(p, key);
            const char *eol = strstr(p, "\r\n");
            if (!k || (eol && k > eol)) return;
            k += strlen(key);
            const char *e = strchr(k, '"');
            if (!e) return;
            size_t l = min((size_t)(e - k), n - 1);
            memcpy(out, k, l); out[l] = 0;
        };
        if (!strncasecmp(p, "Digest", 6)) {
            a.digest = true;
            val("realm=\"", a.realm, sizeof(a.realm));
            val("nonce=\"", a.nonce, sizeof(a.nonce));
            val("opaque=\"", a.opaque, sizeof(a.opaque));
        } else if (!strncasecmp(p, "Basic", 5)) a.basic = true;
    }
}

static void authHeader(const PushUrl &u, const PushAuth &a, const char *method, const char *uri,
                       char *out, size_t n) {
    out[0] = 0;
    if (!u.user[0]) return;
    if (a.digest) {
        char tmp[320], ha1[33], ha2[33], resp[33];
        snprintf(tmp, sizeof(tmp), "%s:%s:%s", u.user, a.realm, u.pass); md5hex(tmp, ha1);
        snprintf(tmp, sizeof(tmp), "%s:%s", method, uri);               md5hex(tmp, ha2);
        snprintf(tmp, sizeof(tmp), "%s:%s:%s", ha1, a.nonce, ha2);      md5hex(tmp, resp);
        int l = snprintf(out, n, "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"",
                         u.user, a.realm, a.nonce, uri, resp);
        if (a.opaque[0] && l > 0 && (size_t)l < n) l += snprintf(out + l, n - l, ", opaque=\"%s\"", a.opaque);
        if (l > 0 && (size_t)l < n) snprintf(out + l, n - l, "\r\n");
    } else if (a.basic) {
        char up[120], b64[170];
        snprintf(up, sizeof(up), "%s:%s", u.user, u.pass);
        size_t ol = 0;
        mbedtls_base64_encode((unsigned char *)b64, sizeof(b64), &ol, (const unsigned char *)up, strlen(up));
        b64[ol] = 0;
        snprintf(out, n, "Authorization: Basic %s\r\n", b64);
    }
}

// Saada päring; 401 korral loe autentimisviis ja proovi üks kord uuesti
static int pushRequest(Session &s, const PushUrl &u, PushAuth &a, int &cseq, const char *method,
                       const char *uri, const char *extra, const char *body, char *resp, size_t rcap) {
    for (int attempt = 0; attempt < 2; attempt++) {
        char auth[400];
        authHeader(u, a, method, uri, auth, sizeof(auth));
        char req[1400];
        int l = snprintf(req, sizeof(req), "%s %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: SimCam/" SIMCAM_VERSION "\r\n%s%s",
                         method, uri, ++cseq, auth, extra ? extra : "");
        if (body) l += snprintf(req + l, sizeof(req) - l, "Content-Length: %u\r\n\r\n%s", (unsigned)strlen(body), body);
        else l += snprintf(req + l, sizeof(req) - l, "\r\n");
        if (l >= (int)sizeof(req) || !sendAll(s.sock, req, l)) return -1;
        int code = readResponse(s.sock, resp, rcap, 8000);
        if (code != 401 || attempt) return code;
        PushAuth na; parseAuth(resp, na);
        if (!u.user[0] || (!na.digest && !na.basic)) return 401;
        a = na;
    }
    return -1;
}

static void pushTask(void *) {
    char *resp = (char *)malloc(2048);
    uint32_t backoff = 5000;
    Camera::Frame fr;
    for (;;) {
        const Settings::Data cfg = Settings::get();
        if (!cfg.pushEnabled || !cfg.pushUrl[0]) {
            pushState("off");
            for (int i = 0; i < 20 && !s_pushKick; i++) vTaskDelay(pdMS_TO_TICKS(100));
            s_pushKick = false;
            continue;
        }
        PushUrl u;
        if (!parsePushUrl(cfg.pushUrl, u)) {
            pushState("error", "Vigane URL (rtsp://[kasutaja:parool@]host[:port]/tee)");
            for (int i = 0; i < 50 && !s_pushKick; i++) vTaskDelay(pdMS_TO_TICKS(100));
            s_pushKick = false;
            continue;
        }
        if (cfg.pushUser[0]) {                       // eraldi väljad on URL-i omadest tähtsamad
            strlcpy(u.user, cfg.pushUser, sizeof(u.user));
            strlcpy(u.pass, cfg.pushPass, sizeof(u.pass));
        }
        portENTER_CRITICAL(&s_pushMux);
        strlcpy(s_push.target, u.clean, sizeof(s_push.target));
        portEXIT_CRITICAL(&s_pushMux);
        pushState("connecting");
        s_pushKick = false;                          // need seaded on juba loetud

        Session *s = new (std::nothrow) Session();
        bool ok = false;
        const char *err = nullptr;
        char errBuf[80];
        if (s && resp) {
            // --- TCP ühendus
            addrinfo hints = {}, *res = nullptr;
            hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
            char port[8]; snprintf(port, sizeof(port), "%u", u.port);
            if (getaddrinfo(u.host, port, &hints, &res) != 0 || !res) err = "Serveri nime ei leitud (DNS)";
            else {
                s->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                timeval to = {10, 0};
                setsockopt(s->sock, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
                setsockopt(s->sock, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
                if (connect(s->sock, res->ai_addr, res->ai_addrlen) != 0) {
                    snprintf(errBuf, sizeof(errBuf), "Ühendus serveriga ebaõnnestus (errno %d)", errno);
                    err = errBuf;
                }
            }
            if (res) freeaddrinfo(res);

            // --- ANNOUNCE / SETUP / RECORD
            PushAuth auth;
            int cseq = 0;
            if (!err) {
                int one = 1;
                setsockopt(s->sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                s->tcp = true;
                s->v.ssrc = esp_random(); s->v.seq = esp_random() & 0xFFFF;
                s->a.ssrc = esp_random(); s->a.seq = esp_random() & 0xFFFF;
                s->audio = Audio::enabled() && cfg.rtspAudio;
                s->aCodec = cfg.micCodec ? 1 : 0;
                char sdp[512];
                int sl = snprintf(sdp, sizeof(sdp),
                    "v=0\r\no=- %lu 1 IN IP4 0.0.0.0\r\ns=%s\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\n"
                    "m=video 0 RTP/AVP 26\r\na=rtpmap:26 JPEG/90000\r\na=control:track1\r\n",
                    (unsigned long)esp_random(), cfg.camName);
                if (s->audio)
                    snprintf(sdp + sl, sizeof(sdp) - sl, s->aCodec ?
                             "m=audio 0 RTP/AVP 97\r\na=rtpmap:97 L16/16000/1\r\na=control:track2\r\n" :
                             "m=audio 0 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\na=control:track2\r\n");
                int code = pushRequest(*s, u, auth, cseq, "ANNOUNCE", u.clean,
                                       "Content-Type: application/sdp\r\n", sdp, resp, 2048);
                if (code != 200) {
                    snprintf(errBuf, sizeof(errBuf), code == 401 ? "Vale kasutaja/parool (401)" :
                             code < 0 ? "Server ei vastanud (ANNOUNCE)" : "Server keeldus: ANNOUNCE %d", code);
                    err = errBuf;
                }
            }
            char session[80] = "";
            for (int t = 0; t < (s->audio ? 2 : 1) && !err; t++) {
                char uri[240], extra[200];
                snprintf(uri, sizeof(uri), "%s%strack%d", u.clean, u.clean[strlen(u.clean) - 1] == '/' ? "" : "/", t + 1);
                int l = snprintf(extra, sizeof(extra), "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;mode=record\r\n", t * 2, t * 2 + 1);
                if (session[0]) snprintf(extra + l, sizeof(extra) - l, "Session: %s\r\n", session);
                int code = pushRequest(*s, u, auth, cseq, "SETUP", uri, extra, nullptr, resp, 2048);
                if (code != 200) { snprintf(errBuf, sizeof(errBuf), "Server keeldus: SETUP %d", code); err = errBuf; break; }
                if (!session[0] && header(resp, "Session", session, sizeof(session))) {
                    char *semi = strchr(session, ';'); if (semi) *semi = 0;
                }
                Track &tr = t ? s->a : s->v;
                tr.chRtp = t * 2; tr.chRtcp = t * 2 + 1; tr.setup = true;
            }
            if (!err) {
                char extra[120];
                snprintf(extra, sizeof(extra), "Session: %s\r\nRange: npt=0.000-\r\n", session);
                int code = pushRequest(*s, u, auth, cseq, "RECORD", u.clean, extra, nullptr, resp, 2048);
                if (code != 200) { snprintf(errBuf, sizeof(errBuf), "Server keeldus: RECORD %d", code); err = errBuf; }
            }

            // --- Voog
            if (!err) {
                ok = true;
                backoff = 5000;
                pushState("streaming", "");
                LOGI(TAG, "Push: saadan → %s%s", u.clean, s->audio ? " (pilt + heli)" : "");
                Camera::addConsumer();
                s->aPos = Audio::position();
                s->aTs = (uint32_t)((uint64_t)Audio::msAt(s->aPos) * audioRate(*s) / 1000);
                uint32_t lastSeq = 0, t0 = millis(), lastKeep = millis(), statT = millis();
                uint32_t frames = 0, bytes0 = 0;
                portENTER_CRITICAL(&s_pushMux);
                s_push.since = millis();
                portEXIT_CRITICAL(&s_pushMux);
                for (;;) {
                    const Settings::Data c2 = Settings::get();
                    if (!c2.pushEnabled || strcmp(c2.pushUrl, cfg.pushUrl) || strcmp(c2.pushUser, cfg.pushUser) ||
                        strcmp(c2.pushPass, cfg.pushPass) || s_pushKick) { s_pushKick = false; err = nullptr; break; }
                    // serveri andmed (RTCP, vastused) – loe ja viska ära
                    fd_set rd; FD_ZERO(&rd); FD_SET(s->sock, &rd);
                    timeval tv = {0, 5000};
                    int r = select(s->sock + 1, &rd, nullptr, nullptr, &tv);
                    if (r > 0) {
                        int n = recv(s->sock, resp, 2047, 0);
                        if (n <= 0) { err = "Server sulges ühenduse"; break; }
                    }
                    if (Camera::waitFrame(fr, lastSeq, 0)) {
                        lastSeq = fr.seq;
                        if (!sendJpeg(*s, fr)) { err = "Saatmine ebaõnnestus (võrk?)"; break; }
                        frames++;
                    }
                    if (s->a.setup && Audio::running() && !sendAudio(*s)) { err = "Heli saatmine ebaõnnestus"; break; }
                    uint32_t now = millis();
                    if (now - s->lastSr >= 5000) {
                        s->lastSr = now;
                        sendSr(*s, s->v, now * 90, now);
                        if (s->a.setup)
                            sendSr(*s, s->a, s->aTs + (int32_t)(now - Audio::msAt(s->aPos)) * (int32_t)audioRate(*s) / 1000, now);
                    }
                    if (now - lastKeep >= 30000) {          // RTSP keep-alive (vastust ei oota)
                        lastKeep = now;
                        char ka[200];
                        int l = snprintf(ka, sizeof(ka), "OPTIONS %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\n\r\n", u.clean, ++cseq, session);
                        if (!sendAll(s->sock, ka, l)) { err = "Saatmine ebaõnnestus (võrk?)"; break; }
                    }
                    if (now - statT >= 2000) {
                        uint32_t oct = s->v.octets + s->a.octets;
                        portENTER_CRITICAL(&s_pushMux);
                        s_push.fps = frames * 1000.0f / (now - statT);
                        s_push.kBps = (oct - bytes0) / 1.024f / (now - statT);
                        s_push.sentMB = oct / 1048576.0f;
                        portEXIT_CRITICAL(&s_pushMux);
                        frames = 0; bytes0 = oct; statT = now;
                    }
                }
                Camera::removeConsumer();
                LOGI(TAG, "Push lõppes pärast %lu s%s%s", (unsigned long)((millis() - t0) / 1000), err ? ": " : "", err ? err : "");
                if (!err) {                                 // seaded muutusid → korralik lõpp
                    char td[200];
                    int l = snprintf(td, sizeof(td), "TEARDOWN %s RTSP/1.0\r\nCSeq: %d\r\nSession: %s\r\n\r\n", u.clean, ++cseq, session);
                    sendAll(s->sock, td, l);
                }
            }
        } else err = "Mälu ei jätkunud";

        if (s) {
            if (s->sock >= 0) { shutdown(s->sock, SHUT_RDWR); close(s->sock); }
            delete s;
        }
        Camera::freeFrame(fr);
        portENTER_CRITICAL(&s_pushMux);
        s_push.fps = 0; s_push.kBps = 0; s_push.since = 0;
        portEXIT_CRITICAL(&s_pushMux);
        if (err) {
            if (!ok) LOGW(TAG, "Push: %s – uus katse %lu s pärast", err, (unsigned long)(backoff / 1000));
            pushState("retry", err);
            portENTER_CRITICAL(&s_pushMux);
            s_push.retries++;
            portEXIT_CRITICAL(&s_pushMux);
            for (uint32_t w = 0; w < backoff && !s_pushKick; w += 100) vTaskDelay(pdMS_TO_TICKS(100));
            s_pushKick = false;
            if (!ok) backoff = min<uint32_t>(backoff * 2, 60000);
        }
    }
}

PushStatus pushStatus() {
    portENTER_CRITICAL(&s_pushMux);
    PushStatus p = s_push;
    portEXIT_CRITICAL(&s_pushMux);
    return p;
}

void pushRestart() { s_pushKick = true; }

bool begin() {
    xTaskCreatePinnedToCore(pushTask, "rtsp_push", 8192, nullptr, 3, nullptr, 1);
    return xTaskCreatePinnedToCore(serverTask, "rtsp_srv", 4096, nullptr, 3, nullptr, 1) == pdPASS;
}

int clients() { return s_clients; }

}  // namespace RtspServer
