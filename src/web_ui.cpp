// =============================================================================
//  Veebiliides – teostus
// =============================================================================
#include "web_ui.h"
#include "web_page.h"
#include "camera_handler.h"
#include "modem_lte.h"
#include "rtsp_server.h"
#include "config.h"
#include "version.h"
#include "auth.h"
#include "settings.h"
#include "wifi_manager.h"
#include "ota.h"
#include "battery.h"
#include "audio.h"
#include "log.h"

#include <Arduino.h>
#include <atomic>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"

static const char *TAG = "WEB";


namespace WebUI {

static httpd_handle_t   s_server = nullptr;
static std::atomic<int> s_streams{0};
static std::atomic<int> s_audioStreams{0};

// Iga MJPEG vaataja statistika (kaadrisagedus ja andmemaht), et veebileht saaks
// näidata, mida see brauser tegelikult kätte saab. Vaataja tuvastatakse
// /stream?id=<juhuslik> parameetriga.
struct ViewStat {
    char     id[16];
    bool     used;
    float    fps;
    float    kBps;
    uint32_t updated;
};
static ViewStat s_views[HTTP_MAX_STREAMS];
static portMUX_TYPE s_viewMux = portMUX_INITIALIZER_UNLOCKED;

struct StreamArg { httpd_req_t *req; int slot; };

static int viewAlloc(const char *id) {
    int slot = -1;
    portENTER_CRITICAL(&s_viewMux);
    for (int i = 0; i < HTTP_MAX_STREAMS; i++)
        if (!s_views[i].used) {
            s_views[i] = {};
            strlcpy(s_views[i].id, id, sizeof(s_views[i].id));
            s_views[i].used = true;
            s_views[i].updated = millis();
            slot = i;
            break;
        }
    portEXIT_CRITICAL(&s_viewMux);
    return slot;
}


#define PART_BOUNDARY "simcamframe"
static const char STREAM_CT[]   = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char STREAM_SEP[]  = "\r\n--" PART_BOUNDARY "\r\n";
static const char STREAM_PART[] = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// --- Autentimine ------------------------------------------------------------
// --- Captive portal ------------------------------------------------------------
// Kas päring tuli hotspoti liidese kaudu? (kohalik soketi aadress = hotspoti IP)
static bool viaHotspot(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_storage ss = {};
    socklen_t l = sizeof(ss);
    if (getsockname(fd, (struct sockaddr *)&ss, &l) != 0) return false;
    uint32_t ip = 0;
    if (ss.ss_family == AF_INET) ip = ((struct sockaddr_in *)&ss)->sin_addr.s_addr;
    else if (ss.ss_family == AF_INET6) {
        // IPv4-mapped IPv6 (::ffff:a.b.c.d)
        struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&ss;
        memcpy(&ip, &a6->sin6_addr.un.u32_addr[3], 4);
    }
    return WifiMgr::isApAddress(ip);
}

// Kas päringul on kehtiv sessiooniküpsis või HTTP Basic päis?
static bool hasAccess(httpd_req_t *req) {
    if (!Auth::enabled()) return true;
    char hdr[160] = "";
    if (httpd_req_get_hdr_value_str(req, "Cookie", hdr, sizeof(hdr)) == ESP_OK && Auth::checkCookie(hdr))
        return true;
    hdr[0] = 0;
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK && Auth::check(hdr))
        return true;
    return false;
}

// API/voog: 401 JSON (ilma WWW-Authenticate päiseta → brauser ei näita hüpikakent;
// leht suunab ise /login-ile). Skriptid võivad kasutada HTTP Basic päist.
static bool authorized(httpd_req_t *req) {
    if (hasAccess(req)) return true;
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"login\"}");
    return false;
}

// HTML lehed: suuna sisselogimisele
static bool authorizedPage(httpd_req_t *req) {
    if (hasAccess(req)) return true;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, "", 0);
    return false;
}

static void logHotspot(httpd_req_t *req) {
    if (!viaHotspot(req)) return;
    char host[64] = "";
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    LOGI(TAG, "Hotspot HTTP: %s%s", host, req->uri);
}

static esp_err_t sendPage(httpd_req_t *req, const char *html) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, strlen_P(html));
}

// --- JSON abi ----------------------------------------------------------------
static void jsonEsc(char *dst, size_t n, const char *src) {
    size_t j = 0;
    for (; *src && j + 2 < n; src++) {
        char c = *src;
        if (c == '"' || c == '\\') { dst[j++] = '\\'; dst[j++] = c; }
        else if ((uint8_t)c >= 0x20) dst[j++] = c;
    }
    dst[j] = 0;
}

// =============================================================================
//  Käsitlejad
// =============================================================================
static esp_err_t h_index(httpd_req_t *req) {
    logHotspot(req);
    if (!authorizedPage(req)) return ESP_OK;
    return sendPage(req, INDEX_HTML);
}

static esp_err_t h_settings(httpd_req_t *req) {
    if (!authorizedPage(req)) return ESP_OK;
    return sendPage(req, SETTINGS_HTML);
}

static esp_err_t h_audio_js(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, AUDIO_JS, strlen_P(AUDIO_JS));
}

static esp_err_t h_style(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, COMMON_CSS, strlen_P(COMMON_CSS));
}

static esp_err_t h_login_get(httpd_req_t *req) {
    logHotspot(req);
    return sendPage(req, LOGIN_HTML);
}

// Loe kogu päringu keha (vormid on väikesed)
static bool readBody(httpd_req_t *req, char *body, size_t cap) {
    int len = req->content_len;
    if (len <= 0 || len >= (int)cap) return false;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) { if (r == HTTPD_SOCK_ERR_TIMEOUT) continue; return false; }
        got += r;
    }
    body[got] = 0;
    return true;
}

static bool formField(const char *body, const char *key, char *out, size_t len);

static esp_err_t h_login_post(httpd_req_t *req) {
    char body[256], pass[80] = "";
    bool ok = readBody(req, body, sizeof(body)) && formField(body, "pass", pass, sizeof(pass)) &&
              Auth::checkPassword(pass);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (ok) {
        static char cookie[128];
        snprintf(cookie, sizeof(cookie), "simcam=%s; Path=/; Max-Age=2592000; HttpOnly; SameSite=Lax",
                 Auth::cookieValue().c_str());
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
        httpd_resp_set_hdr(req, "Location", "/");
        LOGI(TAG, "Sisselogimine õnnestus");
    } else {
        vTaskDelay(pdMS_TO_TICKS(800));                     // aeglustab parooli arvamist
        httpd_resp_set_hdr(req, "Location", "/login?e=1");
        LOGW(TAG, "Vale parool");
    }
    return httpd_resp_send(req, "", 0);
}

static esp_err_t h_logout(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Set-Cookie", "simcam=; Path=/; Max-Age=0");
    httpd_resp_set_hdr(req, "Location", "/login");
    return httpd_resp_send(req, "", 0);
}

static esp_err_t h_password(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    char body[256], pass[80] = "";
    httpd_resp_set_type(req, "application/json");
    if (!readBody(req, body, sizeof(body)) || !formField(body, "pass", pass, sizeof(pass)) ||
        !Auth::setPassword(pass)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false}");
    }
    // Uus küpsis, et kasutaja jääks sisse logituks
    static char cookie[128];
    snprintf(cookie, sizeof(cookie), "simcam=%s; Path=/; Max-Age=2592000; HttpOnly; SameSite=Lax",
             Auth::cookieValue().c_str());
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// --- Aku -------------------------------------------------------------------------
static esp_err_t sendJson(httpd_req_t *req, const String &j);

static String micJson() {
    const Settings::Data d = Settings::get();
    char j[320];
    snprintf(j, sizeof(j),
        "\"mic\":{\"enabled\":%s,\"running\":%s,\"level\":%.1f,\"peak\":%.1f,\"gain\":%d,"
        "\"codec\":%d,\"rtsp\":%s,\"listeners\":%d,\"chan\":%d,\"used_chan\":%d,\"l_db\":%.0f,\"r_db\":%.0f,\"i2s\":\"%s\"}",
        d.micEnabled ? "true" : "false", Audio::running() ? "true" : "false", Audio::levelDb(),
        Audio::peakDb(), d.micGain, d.micCodec, d.rtspAudio ? "true" : "false", (int)s_audioStreams,
        d.micChan, Audio::channel(), Audio::chanDb(0), Audio::chanDb(1), Audio::config());
    return String(j);
}

static String batJson() {
    Battery::Status b = Battery::status();
    char j[260];
    snprintf(j, sizeof(j),
        "\"bat\":{\"enabled\":%s,\"state\":\"%s\",\"v\":%.3f,\"raw\":%.3f,\"pct\":%d,"
        "\"slope\":%.2f,\"min_left\":%d,\"hist_min\":%d,\"usb\":%s,\"cal\":%.4f}",
        b.enabled ? "true" : "false", Battery::stateName(b.state), b.voltage, b.raw, b.percent,
        b.slope, b.minutesLeft, b.histMinutes, b.usbHost ? "true" : "false", Settings::get().batCal);
    return String(j);
}

// GET /api/battery – olek + ajalugu (mV, iga BAT_HIST_SEC s järel, vanim → uusim)
static esp_err_t h_battery(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    uint16_t *h = (uint16_t *)heap_caps_malloc(BAT_HIST_LEN * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    size_t n = h ? Battery::history(h, BAT_HIST_LEN) : 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    String head = "{" + batJson() + ",\"step_s\":" + String(BAT_HIST_SEC) + ",\"hist\":[";
    httpd_resp_send_chunk(req, head.c_str(), head.length());
    char buf[1024];
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        len += snprintf(buf + len, sizeof(buf) - len, i ? ",%u" : "%u", h[i]);
        if (len > sizeof(buf) - 16) { httpd_resp_send_chunk(req, buf, len); len = 0; }
    }
    if (len) httpd_resp_send_chunk(req, buf, len);
    free(h);
    httpd_resp_send_chunk(req, "]}", 2);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// POST /api/battery: en=0/1 (aku ühendatud), v=<multimeetriga mõõdetud pinge> (kalibreerimine),
// reset_cal=1 (tegur tagasi 1,0)
static esp_err_t h_battery_post(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    char body[128], v[16];
    if (!readBody(req, body, sizeof(body))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    if (formField(body, "en", v, sizeof(v)) || formField(body, "reset_cal", v, sizeof(v))) {
        Settings::Data d = Settings::get();
        if (formField(body, "en", v, sizeof(v))) d.batEnabled = v[0] == '1';
        if (formField(body, "reset_cal", v, sizeof(v)) && v[0] == '1') d.batCal = 1.0f;
        if (!Settings::save(d)) return sendJson(req, "{\"ok\":false,\"error\":\"Salvestamine ebaõnnestus\"}");
    }
    if (formField(body, "v", v, sizeof(v)) && v[0]) {
        for (char *c = v; *c; c++) if (*c == ',') *c = '.';
        float f = Battery::calibrate(strtof(v, nullptr));
        if (f == 0) return sendJson(req, "{\"ok\":false,\"error\":\"Kalibreerimine ebaõnnestus: pinge peab olema 2,5–4,5 V ja erinema näidust alla 20 %\"}");
    }
    return sendJson(req, "{\"ok\":true}");
}

static esp_err_t h_status(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    LTE::Status m = LTE::status();
    char op[64], err[200], model[64];
    jsonEsc(op, sizeof(op), m.op);
    jsonEsc(err, sizeof(err), m.lastError);
    jsonEsc(model, sizeof(model), m.model);

    uint32_t connS = (m.connected && m.connectedSinceMs) ? (millis() - m.connectedSinceMs) / 1000 : 0;
    const Settings::Data cfgd = Settings::get();
    const char *ip = m.connected ? m.ip : "-";

    WifiMgr::Status w = WifiMgr::status();
    char staSsid[70], apSsid[70];
    jsonEsc(staSsid, sizeof(staSsid), w.staSsid);
    jsonEsc(apSsid, sizeof(apSsid), w.apSsid);

    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
        "{\"wifi\":{\"sta_en\":%s,\"sta_ok\":%s,\"sta_ssid\":\"%s\",\"sta_ip\":\"%s\","
        "\"sta_rssi\":%d,\"ch\":%d,\"ap_en\":%s,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"ap_clients\":%d,\"ap_temp\":%s},"
        "\"lte\":{\"enabled\":%s,\"state\":\"%s\",\"connected\":%s,\"ip\":\"%s\","
        "\"operator\":\"%s\",\"tech\":\"%s\",\"csq\":%d,\"rssi\":%d,"
        "\"rsrp\":%d,\"rsrq\":%.1f,\"reg\":%d,\"model\":\"%s\",\"imei\":\"%s\","
        "\"baud\":%lu,\"cmux\":%s,\"conn_s\":%lu,\"reconnects\":%lu,\"error\":\"%s\"},"
        "\"cam\":{\"sensor\":\"%s\",\"res\":\"%s\",\"fps\":%.2f,\"frame_kb\":%.1f,"
        "\"af\":\"%s\",\"af_ok\":%s,\"consumers\":%d,\"rotate\":%d},"
        "\"sys\":{\"uptime\":%llu,\"heap_free\":%u,\"heap_total\":%u,\"heap_min\":%u,"
        "\"psram_free\":%u,\"psram_total\":%u,\"temp\":%.1f,\"rtsp_clients\":%d,"
        "\"http_streams\":%d,\"rtsp_auth\":%s,\"rtsp_url\":\"rtsp://%s:%d%s\",\"fw\":\"%s\","
        "\"reset\":\"%s\",\"log_w\":%lu,\"log_e\":%lu},%s}",
        w.staEnabled ? "true" : "false", w.staConnected ? "true" : "false", staSsid, w.staIp,
        w.staRssi, w.channel, w.apEnabled ? "true" : "false", apSsid, w.apIp, w.apClients, w.apTemp ? "true" : "false",
        LTE::enabled() ? "true" : "false",
        LTE::enabled() ? LTE::stateName(m.state) : "DISABLED", m.connected ? "true" : "false", m.ip, op, m.tech, m.csq, m.rssiDbm,
        m.rsrpDbm, m.rsrqDb, m.regStat, model, m.imei,
        (unsigned long)m.baud, m.cmux ? "true" : "false", (unsigned long)connS,
        (unsigned long)m.reconnects, err,
        Camera::sensorName(), Camera::resolutionName(), Camera::fps(),
        Camera::lastFrameBytes() / 1024.0f, Camera::afStatus(),
        Camera::afSupported() ? "true" : "false", Camera::consumers(), Camera::rotation(),
        (unsigned long long)(esp_timer_get_time() / 1000000ULL),
        ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(),
        ESP.getFreePsram(), ESP.getPsramSize(), temperatureRead(),
        RtspServer::clients(), (int)s_streams, (Auth::enabled() && cfgd.rtspAuth) ? "true" : "false", ip, RTSP_PORT, RTSP_PATH,
        SIMCAM_VERSION, Log::resetReason(), (unsigned long)Log::warnings(), (unsigned long)Log::errors(),
        (batJson() + "," + micJson()).c_str());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t h_focus(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    bool ok = Camera::refocus();
    if (!ok) httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"AF not supported\"}");
}

static void rebootTask(void *) {
    vTaskDelay(pdMS_TO_TICKS(800));      // lase HTTP vastusel kohale jõuda
    ESP.restart();
}

static esp_err_t h_reboot(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    LOGW(TAG, "Taaskäivitus veebiliidesest");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    xTaskCreate(rebootTask, "reboot", 2048, nullptr, 1, nullptr);
    return ESP_OK;
}

static esp_err_t h_capture(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    Camera::Frame fr;
    if (!Camera::waitFrame(fr, 0, 3000)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=simcam.jpg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, (const char *)fr.buf, fr.len);
    Camera::freeFrame(fr);
    return r;
}

// --- Seaded: WiFi klient, hotspot, LTE ---------------------------------------
static void urlDecode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && isxdigit((uint8_t)s[1]) && isxdigit((uint8_t)s[2])) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, nullptr, 16);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

// Loe vormi väli; tagastab false, kui välja pole
static bool formField(const char *body, const char *key, char *out, size_t len) {
    if (httpd_query_key_value(body, key, out, len) != ESP_OK) return false;
    urlDecode(out);
    return true;
}

// --- SIM-kaardi PIN-i muutmine (AT+CPWD) --------------------------------------
static esp_err_t h_simpin(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    char body[128], o[16] = "", n[16] = "";
    httpd_resp_set_type(req, "application/json");
    auto bad = [&](const char *e) {
        httpd_resp_set_status(req, "400 Bad Request");
        char b[200];
        snprintf(b, sizeof(b), "{\"ok\":false,\"error\":\"%s\"}", e);
        return httpd_resp_sendstr(req, b);
    };
    if (!readBody(req, body, sizeof(body)) || !formField(body, "old", o, sizeof(o)) ||
        !formField(body, "new", n, sizeof(n)))
        return bad("Vigane päring");
    auto digits = [](const char *p) {
        size_t l = strlen(p);
        if (l < 4 || l > 8) return false;
        for (; *p; p++) if (*p < '0' || *p > '9') return false;
        return true;
    };
    if (!digits(o) || !digits(n)) return bad("PIN peab olema 4–8 numbrit");
    String err;
    if (!LTE::changeSimPin(o, n, err)) {
        char e[160];
        jsonEsc(e, sizeof(e), err.c_str());
        return bad(e);
    }
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// --- FOTA ----------------------------------------------------------------------
static esp_err_t sendJson(httpd_req_t *req, const String &j) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, j.c_str(), j.length());
}

// --- Logi ------------------------------------------------------------------------
static esp_err_t h_log_page(httpd_req_t *req) {
    if (!authorizedPage(req)) return ESP_OK;
    return sendPage(req, LOG_HTML);
}

// GET /api/log[?since=N][&prev=1][&dl=1] – tekst; X-Log-Next = järgmine `since`
static esp_err_t h_log(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    char q[64] = "", v[16];
    httpd_req_get_url_query_str(req, q, sizeof(q));
    bool prev = httpd_query_key_value(q, "prev", v, sizeof(v)) == ESP_OK && v[0] == '1';
    bool dl   = httpd_query_key_value(q, "dl", v, sizeof(v)) == ESP_OK && v[0] == '1';
    uint32_t since = 0;
    if (httpd_query_key_value(q, "since", v, sizeof(v)) == ESP_OK) since = strtoul(v, nullptr, 10);

    char hNext[12], hUp[12], hW[12], hE[12], hDisp[80];
    uint32_t end = Log::position();
    if (since > end) since = 0;                // seade on vahepeal taaskäivitunud
    snprintf(hNext, sizeof(hNext), "%lu", (unsigned long)end);
    snprintf(hUp, sizeof(hUp), "%lu", (unsigned long)millis());
    snprintf(hW, sizeof(hW), "%lu", (unsigned long)Log::warnings());
    snprintf(hE, sizeof(hE), "%lu", (unsigned long)Log::errors());
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Log-Next", hNext);
    httpd_resp_set_hdr(req, "X-Uptime-Ms", hUp);
    httpd_resp_set_hdr(req, "X-Log-Warn", hW);
    httpd_resp_set_hdr(req, "X-Log-Err", hE);
    httpd_resp_set_hdr(req, "X-Reset-Reason", Log::resetCode());
    if (dl) {
        snprintf(hDisp, sizeof(hDisp), "attachment; filename=\"simcam-%s-%s.log\"",
                 SIMCAM_VERSION, prev ? "eelmine" : "logi");
        httpd_resp_set_hdr(req, "Content-Disposition", hDisp);
    }

    if (prev) {
        size_t n;
        const char *p = Log::previous(&n);
        return httpd_resp_send(req, p, n);
    }
    if (dl) {
        char head[160];
        int n = snprintf(head, sizeof(head), "# SimCam %s, tööaeg %lu s, taaskäivituse põhjus: %s\n",
                         SIMCAM_VERSION, (unsigned long)(millis() / 1000), Log::resetReason());
        httpd_resp_send_chunk(req, head, n);
    }
    const size_t CH = 4096;
    char *buf = (char *)malloc(CH);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mälu");
    uint32_t pos = since;
    while (pos < end) {
        uint32_t next;
        size_t want = end - pos < CH ? end - pos : CH;
        size_t n = Log::read(pos, buf, want, &next);
        if (!n) break;
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) { free(buf); return ESP_FAIL; }
        pos = next;
    }
    free(buf);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t h_log_clear(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    Log::clear();
    LOGI(TAG, "Logi tühjendati veebiliidesest");
    return sendJson(req, "{\"ok\":true}");
}

static esp_err_t h_ota(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    return sendJson(req, Ota::statusJson());
}

static esp_err_t h_ota_check(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    Ota::checkNow();
    return sendJson(req, "{\"ok\":true}");
}

static esp_err_t h_ota_update(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    bool ok = Ota::startUpdate();
    if (!ok) httpd_resp_set_status(req, "409 Conflict");
    LOGW(TAG, "Püsivara uuendus veebiliidesest: %s", ok ? "alustatud" : "pole saadaval");
    return sendJson(req, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"Uuendust pole saadaval\"}");
}

// --- Vaataja statistika: /api/view?id=<id> -------------------------------------
static esp_err_t h_view(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    char q[64] = "", id[16] = "";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    httpd_query_key_value(q, "id", id, sizeof(id));
    float fps = -1, kBps = -1;
    portENTER_CRITICAL(&s_viewMux);
    for (auto &v : s_views)
        if (v.used && id[0] && !strcmp(v.id, id)) { fps = v.fps; kBps = v.kBps; }
    portEXIT_CRITICAL(&s_viewMux);
    Battery::Status bs = Battery::status();
    char b[320];
    snprintf(b, sizeof(b), "{\"fps\":%.1f,\"kBps\":%.1f,\"cam_fps\":%.1f,\"rotate\":%d,\"frame_kb\":%.1f,"
             "\"bat\":{\"enabled\":%s,\"state\":\"%s\",\"v\":%.2f,\"pct\":%d},"
             "\"mic\":{\"on\":%s,\"level\":%.1f}}",
             fps, kBps, Camera::fps(), Camera::rotation(), Camera::lastFrameBytes() / 1024.0f,
             bs.enabled ? "true" : "false", Battery::stateName(bs.state), bs.voltage, bs.percent,
             Audio::running() ? "true" : "false", Audio::levelDb());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, b);
}

static esp_err_t h_favicon(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, "", 0);
}

static esp_err_t h_config_get(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    Settings::Data d = Settings::get();
    char ss[70], as[70], apn[130], buf[1400];
    jsonEsc(ss, sizeof(ss), d.staSsid);
    jsonEsc(as, sizeof(as), d.apSsid);
    jsonEsc(apn, sizeof(apn), d.apn);
    // Paroole ei saadeta kunagi välja – ainult info, kas need on määratud
    int n = snprintf(buf, sizeof(buf),
        "{\"sta_en\":%s,\"sta_ssid\":\"%s\",\"sta_has_pass\":%s,"
        "\"ap_en\":%s,\"ap_ssid\":\"%s\",\"ap_has_pass\":%s,\"lte_en\":%s,"
        "\"apn\":\"%s\",\"has_pin\":%s,\"default_pass\":%s,\"rtsp_auth\":%s,\"auto_update\":%s,"
        "\"mic_en\":%s,\"mic_gain\":%d,\"mic_codec\":%d,\"mic_chan\":%d,\"rtsp_audio\":%s,"
        "\"framesize\":%d,\"framesizes\":%s}",
        d.staEnabled ? "true" : "false", ss, d.staPass[0] ? "true" : "false",
        d.apEnabled ? "true" : "false", as, d.apPass[0] ? "true" : "false",
        d.lteEnabled ? "true" : "false", apn, d.simPin[0] ? "true" : "false",
        strcmp(d.webPass, WEB_PASS_DEFAULT) == 0 ? "true" : "false", d.rtspAuth ? "true" : "false",
        d.autoUpdate ? "true" : "false",
        d.micEnabled ? "true" : "false", d.micGain, d.micCodec, d.micChan, d.rtspAudio ? "true" : "false",
        Camera::framesize(), Camera::framesizesJson().c_str());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

static void wifiRestartTask(void *) {
    vTaskDelay(pdMS_TO_TICKS(700));      // lase vastusel enne WiFi taaskäivitust kohale jõuda
    WifiMgr::restart();
    vTaskDelete(nullptr);
}

static esp_err_t h_config_post(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    char body[512];
    if (!readBody(req, body, sizeof(body))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    char fsv[8];
    if (formField(body, "framesize", fsv, sizeof(fsv)) && fsv[0]) {   // resolutsioon: rakendub kohe
        if (!Camera::setFramesize(atoi(fsv)))
            return sendJson(req, "{\"ok\":false,\"error\":\"Seda resolutsiooni see sensor ei toeta\"}");
    }

    Settings::Data d = Settings::get();
    const bool oldLte = d.lteEnabled;
    const Settings::Data before = d;
    char v[70];
    if (formField(body, "sta_en", v, sizeof(v))) d.staEnabled = v[0] == '1';
    if (formField(body, "sta_ssid", v, sizeof(v))) strlcpy(d.staSsid, v, sizeof(d.staSsid));
    if (formField(body, "sta_pass", v, sizeof(v)) && v[0]) strlcpy(d.staPass, v, sizeof(d.staPass));
    if (formField(body, "ap_en", v, sizeof(v))) d.apEnabled = v[0] == '1';
    if (formField(body, "ap_ssid", v, sizeof(v))) strlcpy(d.apSsid, v, sizeof(d.apSsid));
    if (formField(body, "ap_pass", v, sizeof(v)) && v[0]) strlcpy(d.apPass, v, sizeof(d.apPass));
    if (formField(body, "lte_en", v, sizeof(v))) d.lteEnabled = v[0] == '1';
    if (formField(body, "apn", v, sizeof(v))) strlcpy(d.apn, v, sizeof(d.apn));
    if (formField(body, "sim_pin", v, sizeof(v)) && v[0]) strlcpy(d.simPin, v, sizeof(d.simPin));
    if (formField(body, "sim_pin_clear", v, sizeof(v)) && v[0] == '1') d.simPin[0] = 0;
    if (formField(body, "rtsp_auth", v, sizeof(v))) d.rtspAuth = v[0] == '1';
    if (formField(body, "mic_en", v, sizeof(v))) d.micEnabled = v[0] == '1';
    if (formField(body, "mic_gain", v, sizeof(v))) d.micGain = constrain(atoi(v), 0, 40);
    if (formField(body, "mic_codec", v, sizeof(v))) d.micCodec = v[0] == '1' ? 1 : 0;
    if (formField(body, "mic_chan", v, sizeof(v))) d.micChan = constrain(atoi(v), 0, 2);
    if (formField(body, "rtsp_audio", v, sizeof(v))) d.rtspAudio = v[0] == '1';
    bool autoOn = false;
    if (formField(body, "auto_update", v, sizeof(v))) { autoOn = v[0] == '1' && !d.autoUpdate; d.autoUpdate = v[0] == '1'; }
    const bool apForced = Settings::applyFailsafe(d);
    const bool modemChanged = strcmp(d.apn, Settings::get().apn) || strcmp(d.simPin, Settings::get().simPin);

    httpd_resp_set_type(req, "application/json");
    if (!Settings::save(d)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req,
            "{\"ok\":false,\"error\":\"Vigased seaded: SSID ei tohi olla tühi, hotspoti parool min 8 märki, PIN ainult numbrid\"}");
    }
    bool reboot = d.lteEnabled != oldLte || (d.lteEnabled && modemChanged);   // rakendub taaskäivitusel
    char resp[80];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"reboot\":%s,\"ap_forced\":%s}",
             reboot ? "true" : "false", apForced ? "true" : "false");
    httpd_resp_sendstr(req, resp);
    LOGI(TAG, "Seaded muudetud veebiliidesest%s", reboot ? " (LTE muutus → vajab taaskäivitust)" : "");
    if (before.micEnabled != d.micEnabled || before.micGain != d.micGain || before.micChan != d.micChan)
        Audio::apply();
    // WiFi taaskäivitus ainult siis, kui WiFi/hotspoti seaded tegelikult muutusid
    // (muidu katkeks nt RTSP ja käimasolev uuenduste kontroll asjatult).
    const Settings::Data after = Settings::get();
    const bool wifiChanged = before.staEnabled != after.staEnabled || before.apEnabled != after.apEnabled ||
        strcmp(before.staSsid, after.staSsid) || strcmp(before.staPass, after.staPass) ||
        strcmp(before.apSsid, after.apSsid) || strcmp(before.apPass, after.apPass);
    if (wifiChanged) xTaskCreate(wifiRestartTask, "wifi_rst", 4096, nullptr, 2, nullptr);
    if (autoOn) Ota::checkNow(wifiChanged ? 8000 : 0);   // automaatika sisse → kontrolli kohe
    return ESP_OK;
}

static esp_err_t h_scan(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    String j = WifiMgr::scanJson();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, j.c_str(), j.length());
}

// --- Kaamera seaded: /api/cam?var=X&val=Y  |  /api/cam?reg=0x3500  |  /api/cam
static esp_err_t h_cam(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    char q[96] = "", var[24] = "", val[16] = "", reg[16] = "";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (httpd_query_key_value(q, "reg", reg, sizeof(reg)) == ESP_OK) {
        int r = (int)strtol(reg, nullptr, 0);
        char b[48];
        snprintf(b, sizeof(b), "{\"reg\":%d,\"val\":%d}", r, Camera::readReg(r));
        return httpd_resp_sendstr(req, b);
    }
    if (httpd_query_key_value(q, "var", var, sizeof(var)) == ESP_OK &&
        httpd_query_key_value(q, "val", val, sizeof(val)) == ESP_OK) {
        if (!Camera::control(var, atoi(val))) {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "{\"ok\":false}");
        }
    }
    String j = Camera::settingsJson();
    return httpd_resp_send(req, j.c_str(), j.length());
}

// --- MJPEG voog (asünkroonne) -----------------------------------------------
static void streamTask(void *arg) {
    StreamArg sa = *(StreamArg *)arg;
    delete (StreamArg *)arg;
    httpd_req_t *req = sa.req;
    uint32_t winStart = millis(), winFrames = 0, winBytes = 0;
    Camera::Frame fr;
    uint32_t lastSeq = 0;
    char part[64];
    Camera::addConsumer();
    LOGI(TAG, "MJPEG vaataja lisandus (%d)", (int)s_streams);

    httpd_resp_set_type(req, STREAM_CT);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Framerate", "15");

    for (;;) {
        if (!Camera::waitFrame(fr, lastSeq, 5000)) continue;   // kaamera ootel
        lastSeq = fr.seq;
        int n = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fr.len);
        if (httpd_resp_send_chunk(req, STREAM_SEP, strlen(STREAM_SEP)) != ESP_OK) break;
        if (httpd_resp_send_chunk(req, part, n) != ESP_OK) break;
        if (httpd_resp_send_chunk(req, (const char *)fr.buf, fr.len) != ESP_OK) break;
        winFrames++;
        winBytes += fr.len + n + strlen(STREAM_SEP);
        uint32_t now = millis();
        if (sa.slot >= 0 && now - winStart >= 2000) {
            float dt = (now - winStart) / 1000.0f;
            portENTER_CRITICAL(&s_viewMux);
            s_views[sa.slot].fps = winFrames / dt;
            s_views[sa.slot].kBps = winBytes / 1024.0f / dt;
            s_views[sa.slot].updated = now;
            portEXIT_CRITICAL(&s_viewMux);
            winStart = now; winFrames = winBytes = 0;
        }
    }

    if (sa.slot >= 0) { portENTER_CRITICAL(&s_viewMux); s_views[sa.slot].used = false; portEXIT_CRITICAL(&s_viewMux); }
    Camera::freeFrame(fr);
    Camera::removeConsumer();
    httpd_req_async_handler_complete(req);
    s_streams--;
    LOGI(TAG, "MJPEG vaataja lahkus");
    vTaskDelete(nullptr);
}

// --- Heli brauserisse: /audio --------------------------------------------------
// Toores heli jupitatud HTTP vastusena (fetch + Web Audio brauseris):
//   X-Audio-Format: mulaw (8 kHz, 1 bait/diskreet) või s16le (16 kHz)
static void audioTask(void *arg) {
    httpd_req_t *req = (httpd_req_t *)arg;
    const bool l16 = Settings::get().micCodec == 1;
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Audio-Format", l16 ? "s16le" : "mulaw");
    httpd_resp_set_hdr(req, "X-Audio-Rate", l16 ? "16000" : "8000");
    LOGI(TAG, "Brauseri helivoog lisandus (%d)", (int)s_audioStreams);

    // Puhvrid kuhjas, mitte pinus: varem 2,6 kB pinus + lwIP saatmine + logi
    // ületas 4 kB pinu → seade taaskäivitus (v1.6.0–v1.8.0 "Kuula" nupp).
    const size_t N = MIC_RATE / 25;                           // 40 ms
    int16_t *pcm = (int16_t *)malloc(N * sizeof(int16_t));
    uint8_t *out = (uint8_t *)malloc(N * 2);
    uint32_t pos = Audio::position(), skipped;
    uint32_t idle = millis();
    for (;pcm && out;) {
        if (!Audio::running()) {
            if (millis() - idle > 3000) break;                // mikrofon lülitati välja
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        idle = millis();
        if (!Audio::read(&pos, pcm, N, 500, &skipped)) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        size_t len;
        if (l16) { memcpy(out, pcm, N * 2); len = N * 2; }
        else len = Audio::encodePcmu(pcm, N, out);
        if (httpd_resp_send_chunk(req, (const char *)out, len) != ESP_OK) break;
    }
    free(pcm);
    free(out);
    httpd_resp_send_chunk(req, nullptr, 0);
    httpd_req_async_handler_complete(req);
    s_audioStreams--;
    LOGI(TAG, "Brauseri helivoog lõppes (pinu vaba min %u B)", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    vTaskDelete(nullptr);
}

static esp_err_t h_audio(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    if (!Audio::running()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Mikrofon on välja lülitatud");
    }
    if (s_audioStreams >= AUDIO_HTTP_MAX) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Liiga palju kuulajaid");
    }
    httpd_req_t *copy = nullptr;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) return ESP_FAIL;
    s_audioStreams++;
    if (xTaskCreatePinnedToCore(audioTask, "audio_http", 6144, copy, 4, nullptr, 1) != pdPASS) {
        s_audioStreams--;
        httpd_req_async_handler_complete(copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t h_stream(httpd_req_t *req) {
    if (!authorized(req)) return ESP_OK;
    if (s_streams >= HTTP_MAX_STREAMS) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Liiga palju vaatajaid");
    }
    char q[64] = "", id[16] = "";
    httpd_req_get_url_query_str(req, q, sizeof(q));
    httpd_query_key_value(q, "id", id, sizeof(id));
    httpd_req_t *copy = nullptr;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) return ESP_FAIL;
    s_streams++;
    StreamArg *sa = new StreamArg{copy, id[0] ? viewAlloc(id) : -1};
    if (xTaskCreatePinnedToCore(streamTask, "mjpeg", 6144, sa, 3, nullptr, 1) != pdPASS) {
        if (sa->slot >= 0) s_views[sa->slot].used = false;
        delete sa;
        s_streams--;
        httpd_req_async_handler_complete(copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Telefonide internetikontrollid (Android /generate_204, iOS /hotspot-detect.html,
// Windows /connecttest.txt, Firefox /canonical.html …) ja kõik muud tundmatud
// aadressid suunatakse hotspotis kaamera lehele → avaneb automaatselt.
static esp_err_t h_notfound(httpd_req_t *req, httpd_err_code_t) {
    if (viaHotspot(req)) {
        char host[64] = "";
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
        LOGI(TAG, "Captive: %s%s → http://" WIFI_AP_IP_STR "/", host, req->uri);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://" WIFI_AP_IP_STR "/");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, "", 0);
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_FAIL;
}

// =============================================================================
bool begin() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = HTTP_PORT;
    cfg.ctrl_port        = 32768;
    cfg.max_uri_handlers = 40;
    cfg.max_open_sockets = 7;
    cfg.lru_purge_enable = true;          // vabasta vanimad, kui soketid otsas
    cfg.stack_size       = 8192;
    cfg.core_id          = 1;
    cfg.recv_wait_timeout = 10;           // aeglane mobiilivõrk
    cfg.send_wait_timeout = 10;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        LOGE(TAG, "HTTP serveri käivitamine ebaõnnestus");
        return false;
    }
    const httpd_uri_t uris[] = {
        {"/",           HTTP_GET,  h_index,   nullptr},
        {"/settings",   HTTP_GET,  h_settings, nullptr},
        {"/style.css",  HTTP_GET,  h_style,   nullptr},
        {"/login",      HTTP_GET,  h_login_get,  nullptr},
        {"/login",      HTTP_POST, h_login_post, nullptr},
        {"/logout",     HTTP_GET,  h_logout,  nullptr},
        {"/api/password", HTTP_POST, h_password, nullptr},
        {"/api/status", HTTP_GET,  h_status,  nullptr},
        {"/api/focus",  HTTP_POST, h_focus,   nullptr},
        {"/api/reboot", HTTP_POST, h_reboot,  nullptr},
        {"/capture",    HTTP_GET,  h_capture, nullptr},
        {"/stream",     HTTP_GET,  h_stream,  nullptr},
        {"/audio",      HTTP_GET,  h_audio,   nullptr},
        {"/audio.js",   HTTP_GET,  h_audio_js, nullptr},
        {"/api/config", HTTP_GET,  h_config_get,  nullptr},
        {"/api/config", HTTP_POST, h_config_post, nullptr},
        {"/api/scan",   HTTP_GET,  h_scan,    nullptr},
        {"/api/cam",    HTTP_GET,  h_cam,     nullptr},
        {"/favicon.ico", HTTP_GET, h_favicon, nullptr},
        {"/api/ota",    HTTP_GET,  h_ota,     nullptr},
        {"/api/simpin", HTTP_POST, h_simpin,  nullptr},
        {"/api/view",   HTTP_GET,  h_view,    nullptr},
        {"/api/ota/check",  HTTP_POST, h_ota_check,  nullptr},
        {"/api/ota/update", HTTP_POST, h_ota_update, nullptr},
        {"/log",        HTTP_GET,  h_log_page, nullptr},
        {"/api/log",    HTTP_GET,  h_log,     nullptr},
        {"/api/log/clear", HTTP_POST, h_log_clear, nullptr},
        {"/api/battery", HTTP_GET,  h_battery,      nullptr},
        {"/api/battery", HTTP_POST, h_battery_post, nullptr},
    };
    for (const auto &u : uris) httpd_register_uri_handler(s_server, &u);
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, h_notfound);
    LOGI(TAG, "Veebiliides pordil %d", HTTP_PORT);
    return true;
}

int streamClients() { return s_streams; }

}  // namespace WebUI
