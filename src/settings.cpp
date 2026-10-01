// =============================================================================
//  Püsivad seaded – teostus
// =============================================================================
#include "settings.h"
#include "config.h"
#include "log.h"
#include <Preferences.h>

static const char *TAG = "CFG";
static const char *NS  = "simcamcfg";

namespace Settings {

static Data              s_d;
static SemaphoreHandle_t s_mtx = nullptr;

static void defaults(Data &d) {
    d.staEnabled = WIFI_STA_ENABLED_DEFAULT;
    strlcpy(d.staSsid, WIFI_STA_SSID_DEFAULT, sizeof(d.staSsid));
    strlcpy(d.staPass, WIFI_STA_PASS_DEFAULT, sizeof(d.staPass));
    d.apEnabled = WIFI_AP_ENABLED_DEFAULT;
    strlcpy(d.apSsid, WIFI_AP_SSID_DEFAULT, sizeof(d.apSsid));
    strlcpy(d.apPass, WIFI_AP_PASS_DEFAULT, sizeof(d.apPass));
    d.lteEnabled = LTE_ENABLED_DEFAULT;
    d.rotation = CAM_ROTATION_DEFAULT;
    d.framesize = CAM_FRAME_SIZE;
    d.simPin[0] = 0;
    d.apn[0] = 0;
    strlcpy(d.webPass, WEB_PASS_DEFAULT, sizeof(d.webPass));
    d.authSalt[0] = 0;
    d.rtspAuth = true;
    d.autoUpdate = false;
    d.batEnabled = true;
    d.batCal = 1.0f;
    d.micEnabled = false;
    d.micGain = 24;
    d.micCodec = 0;
    d.rtspAudio = true;
}

bool applyFailsafe(Data &d) {
    if (!d.staEnabled && !d.lteEnabled && !d.apEnabled) {
        d.apEnabled = true;
        if (strlen(d.apPass) < 8) strlcpy(d.apPass, WIFI_AP_PASS_DEFAULT, sizeof(d.apPass));
        if (!d.apSsid[0]) strlcpy(d.apSsid, WIFI_AP_SSID_DEFAULT, sizeof(d.apSsid));
        return true;
    }
    return false;
}

void load() {
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    Data d;
    defaults(d);
    Preferences p;
    if (p.begin(NS, true)) {
        d.staEnabled = p.getBool("sta_en", d.staEnabled);
        if (p.isKey("sta_ssid")) p.getString("sta_ssid", d.staSsid, sizeof(d.staSsid));
        if (p.isKey("sta_pass")) p.getString("sta_pass", d.staPass, sizeof(d.staPass));
        d.apEnabled = p.getBool("ap_en", d.apEnabled);
        if (p.isKey("ap_ssid")) p.getString("ap_ssid", d.apSsid, sizeof(d.apSsid));
        if (p.isKey("ap_pass")) p.getString("ap_pass", d.apPass, sizeof(d.apPass));
        d.lteEnabled = p.getBool("lte_en", d.lteEnabled);
        d.rotation = p.getShort("rot", d.rotation);
        if (p.isKey("sim_pin")) p.getString("sim_pin", d.simPin, sizeof(d.simPin));
        if (p.isKey("apn")) p.getString("apn", d.apn, sizeof(d.apn));
        if (p.isKey("web_pass")) p.getString("web_pass", d.webPass, sizeof(d.webPass));
        d.rtspAuth = p.getBool("rtsp_auth", d.rtspAuth);
        d.autoUpdate = p.getBool("auto_upd", d.autoUpdate);
        d.batEnabled = p.getBool("bat_en", d.batEnabled);
        d.batCal = p.getFloat("bat_cal", d.batCal);
        if (!(d.batCal > 0.8f && d.batCal < 1.25f)) d.batCal = 1.0f;
        d.framesize = p.getInt("framesize", d.framesize);
        d.micEnabled = p.getBool("mic_en", d.micEnabled);
        d.micGain = constrain(p.getInt("mic_gain", d.micGain), 0, 40);
        d.micCodec = p.getInt("mic_codec", d.micCodec) == 1 ? 1 : 0;
        d.rtspAudio = p.getBool("rtsp_audio", d.rtspAudio);
        if (p.isKey("salt")) p.getString("salt", d.authSalt, sizeof(d.authSalt));
        p.end();
    }
    // Sool luuakse esimesel käivitusel (küpsis ei ole parooli järgi äraarvatav)
    if (!d.authSalt[0]) {
        snprintf(d.authSalt, sizeof(d.authSalt), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
        Preferences w;
        if (w.begin(NS, false)) { w.putString("salt", d.authSalt); w.end(); }
    }
    if (applyFailsafe(d)) LOGW(TAG, "Failsafe: WiFi klient ja LTE väljas → hotspot sisse");
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_d = d;
    xSemaphoreGive(s_mtx);
    LOGI(TAG, "WiFi klient: %s (%s), hotspot: %s (%s), LTE: %s (APN '%s', PIN %s)",
         d.staEnabled ? "sees" : "väljas", d.staSsid,
         d.apEnabled ? "sees" : "väljas", d.apSsid, d.lteEnabled ? "sees" : "väljas",
         d.apn, d.simPin[0] ? "määratud" : "puudub");
}

Data get() {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    Data c = s_d;
    xSemaphoreGive(s_mtx);
    return c;
}

bool save(const Data &din) {
    Data d = din;
    applyFailsafe(d);
    // WPA2 parool peab olema 8–63 märki (või tühi = avatud võrk kliendi puhul)
    if (d.apEnabled && (strlen(d.apSsid) == 0 || strlen(d.apPass) < 8)) return false;
    if (d.staEnabled && strlen(d.staSsid) == 0) return false;
    if (d.rotation % 90 != 0 || d.rotation < 0 || d.rotation > 270) return false;
    if (strlen(d.webPass) < 4) return false;
    if (!(d.batCal > 0.8f && d.batCal < 1.25f)) return false;
    if (d.micGain < 0 || d.micGain > 40 || (d.micCodec != 0 && d.micCodec != 1)) return false;
    for (const char *c = d.simPin; *c; c++) if (*c < '0' || *c > '9') return false;   // PIN = numbrid
    Preferences p;
    if (!p.begin(NS, false)) return false;
    p.putBool("sta_en", d.staEnabled);
    p.putString("sta_ssid", d.staSsid);
    p.putString("sta_pass", d.staPass);
    p.putBool("ap_en", d.apEnabled);
    p.putString("ap_ssid", d.apSsid);
    p.putString("ap_pass", d.apPass);
    p.putBool("lte_en", d.lteEnabled);
    p.putShort("rot", d.rotation);
    p.putString("sim_pin", d.simPin);
    p.putString("apn", d.apn);
    p.putString("web_pass", d.webPass);
    p.putString("salt", d.authSalt);
    p.putBool("rtsp_auth", d.rtspAuth);
    p.putBool("auto_upd", d.autoUpdate);
    p.putBool("bat_en", d.batEnabled);
    p.putFloat("bat_cal", d.batCal);
    p.putInt("framesize", d.framesize);
    p.putBool("mic_en", d.micEnabled);
    p.putInt("mic_gain", d.micGain);
    p.putInt("mic_codec", d.micCodec);
    p.putBool("rtsp_audio", d.rtspAudio);
    p.end();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_d = d;
    xSemaphoreGive(s_mtx);
    LOGI(TAG, "Seaded salvestatud");
    return true;
}

void resetDefaults() {
    Preferences p;
    if (p.begin(NS, false)) { p.clear(); p.end(); }
    load();
}

}  // namespace Settings
