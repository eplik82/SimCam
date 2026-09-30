// =============================================================================
//  WiFi haldur – teostus
// =============================================================================
#include "wifi_manager.h"
#include "settings.h"
#include "config.h"
#include "log.h"
#include "modem_lte.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include "lwip/sockets.h"

static const char *TAG = "WIFI";

namespace WifiMgr {

static volatile bool s_staUp = false;
static bool s_mdns = false;
static volatile bool s_dnsOn = false;
static TaskHandle_t  s_dnsTask = nullptr;
static int           s_dnsLogged = 0;

// -----------------------------------------------------------------------------
//  Captive portal DNS – vastab igale A-päringule hotspoti IP-ga (WIFI_AP_IP_STR),
//  AAAA ja muud tüübid saavad tühja NOERROR vastuse (telefon ei oota IPv6-t).
//  Esimesed päringud logitakse, et näha, kas telefon meie DNS-i kasutab.
// -----------------------------------------------------------------------------
static void dnsTask(void *) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (sock < 0 || bind(sock, (sockaddr *)&a, sizeof(a)) != 0) {
        LOGE(TAG, "DNS: port 53 bind ebaõnnestus");
        if (sock >= 0) close(sock);
        s_dnsTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    uint8_t buf[512];
    for (;;) {
        sockaddr_in from = {};
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr *)&from, &fl);
        if (n < 17 || !s_dnsOn) continue;
        if (buf[2] & 0x80) continue;                           // vastus, mitte päring
        if ((buf[4] << 8 | buf[5]) != 1) continue;             // ainult 1 küsimus

        // Küsimuse nimi
        char name[128];
        size_t ni = 0;
        int i = 12;
        while (i < n && buf[i] && ni < sizeof(name) - 2) {
            int l = buf[i++];
            if (l & 0xC0 || i + l > n) { i = n; break; }
            if (ni) name[ni++] = '.';
            for (int k = 0; k < l && ni < sizeof(name) - 1; k++) name[ni++] = buf[i++];
        }
        name[ni] = 0;
        if (i + 5 > n) continue;
        i++;                                                    // nime lõpu 0
        uint16_t qtype = buf[i] << 8 | buf[i + 1];
        int qEnd = i + 4;

        bool answerA = (qtype == 1 || qtype == 255);
        // Vastus: päis + küsimus (+ A kirje)
        uint8_t out[512];
        memcpy(out, buf, qEnd);
        out[2] = 0x80 | (buf[2] & 0x01);                     // QR=1, RD kopeeritud
        out[3] = 0x80;                                          // RA=1, RCODE=0
        out[6] = 0; out[7] = answerA ? 1 : 0;                   // ANCOUNT
        out[8] = out[9] = out[10] = out[11] = 0;                // NS, AR
        int o = qEnd;
        if (answerA) {
            const uint8_t rr[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, WIFI_AP_IP_BYTES};
            memcpy(out + o, rr, sizeof(rr));
            o += sizeof(rr);
        }
        sendto(sock, out, o, 0, (sockaddr *)&from, fl);

        if (s_dnsLogged < 60) {
            s_dnsLogged++;
            char ip[16];
            inet_ntoa_r(from.sin_addr, ip, sizeof(ip));
            LOGI(TAG, "DNS %s %s (tüüp %u) → %s", ip, name, qtype, answerA ? WIFI_AP_IP_STR : "tühi");
        }
    }
}

static void onEvent(arduino_event_id_t ev, arduino_event_info_t info) {
    switch (ev) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_staUp = true;
            LOGI(TAG, "WiFi ühendatud: %s  IP %s  (%d dBm)  →  http://%s/  rtsp://%s:%d%s",
                 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                 WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str(),
                 RTSP_PORT, RTSP_PATH);
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (s_staUp) LOGW(TAG, "WiFi ühendus katkes (põhjus %d), taasühendun...",
                              info.wifi_sta_disconnected.reason);
            s_staUp = false;
            break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            LOGI(TAG, "Hotspoti klient ühendus (%d klienti)", WiFi.softAPgetStationNum());
            s_dnsLogged = 0;                          // logi uue kliendi DNS päringud
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
            char ip[16];
            esp_ip4addr_ntoa(&info.wifi_ap_staipassigned.ip, ip, sizeof(ip));
            LOGI(TAG, "Hotspoti klient sai IP %s (DHCP; DNS = %s)", ip, WIFI_AP_IP_STR);
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            LOGI(TAG, "Hotspoti klient lahkus");
            break;
        default: break;
    }
}

static void startMdns() {
    if (s_mdns) return;
    if (MDNS.begin(WIFI_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        MDNS.addService("rtsp", "tcp", RTSP_PORT);
        s_mdns = true;
        LOGI(TAG, "mDNS: http://%s.local/", WIFI_HOSTNAME);
    }
}

static volatile bool s_apTemp = false;       // failsafe'i poolt ajutiselt sisse lülitatud

static void startAp(const Settings::Data &d) {
    if (WiFi.softAP(d.apSsid, d.apPass, WIFI_AP_CHANNEL, 0, 4)) {
        // DHCP peab klientidele DNS-iks andma seadme enda (WIFI_AP_IP).
        // Arduino pakub DNS-i ainult siis, kui see on softAPConfig-is antud –
        // muidu telefon ei jõua captive DNS-ini ja portaal ei avane.
        const IPAddress apIp(WIFI_AP_IP_BYTES);
        IPAddress lease = apIp;
        lease[3] = apIp[3] + 1;                   // DHCP jagab alates .2
        if (!WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0), lease, apIp))
            LOGE(TAG, "softAPConfig (DHCP DNS) ebaõnnestus");
        LOGI(TAG, "Hotspot '%s' sees: http://%s/", d.apSsid, WiFi.softAPIP().toString().c_str());
    } else {
        LOGE(TAG, "Hotspoti käivitamine ebaõnnestus");
    }
    // Captive portal: iga domeeninimi → WIFI_AP_IP. Telefon tuvastab
    // "sisselogimist vajava võrgu" ja avab kaamera veebilehe.
    if (!s_dnsTask) xTaskCreatePinnedToCore(dnsTask, "dns", 4096, nullptr, 3, &s_dnsTask, 0);
    s_dnsLogged = 0;
    s_dnsOn = true;
}

static void apply() {
    Settings::Data d = Settings::get();
    WiFi.persistent(false);                       // seaded on meie NVS-is
    WiFi.setHostname(WIFI_HOSTNAME);

    wifi_mode_t mode = WIFI_OFF;
    if (d.staEnabled && d.apEnabled) mode = WIFI_AP_STA;
    else if (d.staEnabled) mode = WIFI_STA;
    else if (d.apEnabled) mode = WIFI_AP;

    if (WiFi.getMode() != WIFI_OFF) {             // esimesel käivitusel WiFi pole veel üleval
        WiFi.disconnect(false, false);
        WiFi.softAPdisconnect(false);
    }
    s_staUp = false;
    s_dnsOn = false;
    s_apTemp = false;
    WiFi.mode(mode);
    if (mode == WIFI_OFF) { LOGI(TAG, "WiFi väljas"); return; }

    WiFi.setSleep(false);                         // madalam latentsus voogedastusel

    if (d.apEnabled) startAp(d);
    if (d.staEnabled) {
        WiFi.setAutoReconnect(true);
        WiFi.begin(d.staSsid, d.staPass);
        LOGI(TAG, "Ühendun WiFi võrku '%s'...", d.staSsid);
    }
    startMdns();
}

void begin() {
    WiFi.onEvent(onEvent);
    apply();
}

void restart() {
    LOGI(TAG, "Rakendan uued WiFi seaded");
    apply();
}

Status status() {
    Status s;
    Settings::Data d = Settings::get();
    s.staEnabled = d.staEnabled;
    s.apEnabled = d.apEnabled || s_apTemp;
    s.apTemp = s_apTemp;
    strlcpy(s.staSsid, d.staSsid, sizeof(s.staSsid));
    strlcpy(s.apSsid, d.apSsid, sizeof(s.apSsid));
    s.staConnected = s_staUp && WiFi.isConnected();
    if (s.staConnected) {
        strlcpy(s.staIp, WiFi.localIP().toString().c_str(), sizeof(s.staIp));
        s.staRssi = WiFi.RSSI();
    }
    wifi_mode_t m = WiFi.getMode();
    if (m == WIFI_AP || m == WIFI_AP_STA) {
        strlcpy(s.apIp, WiFi.softAPIP().toString().c_str(), sizeof(s.apIp));
        s.apClients = WiFi.softAPgetStationNum();
    }
    s.channel = WiFi.channel();
    return s;
}

// -----------------------------------------------------------------------------
//  Käitusaegne failsafe: kui hotspot on seadetes väljas ja seadmel pole
//  WIFI_FAILSAFE_S sekundit ühtegi võrguühendust (WiFi klient ega LTE), lülitub
//  hotspot ajutiselt sisse. Kui ühendus on taas WIFI_FAILSAFE_S sekundit
//  stabiilne ja hotspotis pole kliente, lülitub ajutine hotspot välja.
// -----------------------------------------------------------------------------
#define WIFI_FAILSAFE_S 120

void loop() {
    static uint32_t lastTick = 0, offlineSince = 0, onlineSince = 0;
    uint32_t now = millis();
    if (now - lastTick < 1000) return;
    lastTick = now;

    Settings::Data d = Settings::get();
    if (d.apEnabled) { offlineSince = onlineSince = 0; return; }   // hotspot on niigi sees

    bool online = (s_staUp && WiFi.isConnected()) || LTE::connected();
    if (online) { offlineSince = 0; if (!onlineSince) onlineSince = now; }
    else        { onlineSince = 0;  if (!offlineSince) offlineSince = now; }

    if (!s_apTemp && offlineSince && now - offlineSince > WIFI_FAILSAFE_S * 1000UL) {
        LOGW(TAG, "Failsafe: %d s ilma võrguühenduseta → hotspot '%s' ajutiselt sisse",
             WIFI_FAILSAFE_S, d.apSsid);
        WiFi.mode(d.staEnabled ? WIFI_AP_STA : WIFI_AP);
        if (d.staEnabled) { WiFi.setAutoReconnect(true); WiFi.begin(d.staSsid, d.staPass); }
        startAp(d);
        s_apTemp = true;
    }
    if (s_apTemp && onlineSince && now - onlineSince > WIFI_FAILSAFE_S * 1000UL &&
        WiFi.softAPgetStationNum() == 0) {
        LOGI(TAG, "Failsafe: ühendus taastunud → ajutine hotspot välja");
        s_dnsOn = false;
        WiFi.softAPdisconnect(true);
        s_apTemp = false;
    }
}

bool apTemporary() { return s_apTemp; }

bool isApAddress(uint32_t ip) {
    wifi_mode_t m = WiFi.getMode();
    if (m != WIFI_AP && m != WIFI_AP_STA) return false;
    return ip == (uint32_t)WiFi.softAPIP();
}

String scanJson() {
    // Sünkroonne skaneerimine (~2–4 s). AP_STA režiimis võib hotspot hetkeks katkeda.
    wifi_mode_t m = WiFi.getMode();
    if (m == WIFI_AP) WiFi.mode(WIFI_AP_STA);
    int n = WiFi.scanNetworks(false, false);
    String out = "[";
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        if (out.length() > 1) out += ",";
        out += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
               ",\"enc\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0) + "}";
    }
    out += "]";
    WiFi.scanDelete();
    if (m == WIFI_AP) WiFi.mode(WIFI_AP);
    return out;
}

}  // namespace WifiMgr
