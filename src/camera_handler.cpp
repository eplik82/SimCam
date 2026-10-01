// =============================================================================
//  Camera_Manager – teostus
// =============================================================================
#include "camera_handler.h"
#include "config.h"
#include "log.h"

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include <atomic>
#include "ESP32_OV5640_AF.h"   // OV5640 AF püsivara laadija (0015/ESP32-OV5640-AF)
#include "settings.h"

static const char *TAG = "CAM";

namespace Camera {

// --- OV5640 AF firmware registrid -------------------------------------------
// OV5640 sees on 8051 mikrokontroller, mis juhib VCM (voice coil) läätsemootorit.
// Püsivara laetakse SCCB (I2C) kaudu; käsud antakse samuti SCCB registritega.
static constexpr uint16_t AF_CMD_MAIN   = 0x3022;
static constexpr uint16_t AF_CMD_ACK    = 0x3023;
static constexpr uint16_t AF_FW_STATUS  = 0x3029;
static constexpr uint8_t  AF_TRIGGER    = 0x03;  // ühekordne fookus
static constexpr uint8_t  AF_CONTINUOUS = 0x04;  // pidev fookus
static constexpr uint8_t  AF_ST_FOCUSING = 0x00;
static constexpr uint8_t  AF_ST_FOCUSED  = 0x10;
static constexpr uint8_t  AF_ST_IDLE     = 0x70;
static constexpr uint8_t  AF_ST_INIT     = 0x7E;
static constexpr uint8_t  AF_ST_NOFW     = 0x7F;

// --- Olek ------------------------------------------------------------------
static bool              s_ready = false;
static bool              s_afOk = false;
static OV5640            s_ov5640;
static SemaphoreHandle_t s_mtx = nullptr;

// Jagatud "viimane kaader"
static uint8_t  *s_buf = nullptr;
static size_t    s_len = 0, s_cap = 0;
static uint32_t  s_seq = 0;
static uint16_t  s_w = 0, s_h = 0;
static uint32_t  s_ts = 0;

static volatile float    s_fps = 0;
static std::atomic<int>  s_consumers{0};
static volatile bool     s_refocusReq = false;
static volatile uint32_t s_lastUse = 0;        // millis() viimasest kaadripäringust
static volatile bool     s_sleeping = false;   // andur ooterežiimis (keegi ei vaata)
static volatile uint8_t  s_afRaw = 0xFF;
static const char       *s_sensor = "unknown";

// --- Pööramine -------------------------------------------------------------
static volatile int      s_rotation = 0;        // 0 / 180
static int               s_framesize = CAM_FRAME_SIZE;
static framesize_t       s_maxFs = CAM_FRAME_SIZE_MAX;
static bool              s_userVflip = false, s_userHmirror = false;

// --- Abi -------------------------------------------------------------------
static bool ensureCap(uint8_t *&buf, size_t &cap, size_t need) {
    if (cap >= need) return true;
    size_t ncap = need + need / 4 + 1024;        // varu, et vältida sagedast realloc-i
    auto *nb = (uint8_t *)heap_caps_realloc(buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nb) return false;
    buf = nb;
    cap = ncap;
    return true;
}

static camera_config_t makeConfig() {
    camera_config_t c = {};
    c.pin_pwdn     = CAM_PWDN_PIN;
    c.pin_reset    = CAM_RESET_PIN;
    c.pin_xclk     = CAM_XCLK_PIN;
    c.pin_sccb_sda = CAM_SIOD_PIN;
    c.pin_sccb_scl = CAM_SIOC_PIN;
    c.pin_d7 = CAM_Y9_PIN;  c.pin_d6 = CAM_Y8_PIN;
    c.pin_d5 = CAM_Y7_PIN;  c.pin_d4 = CAM_Y6_PIN;
    c.pin_d3 = CAM_Y5_PIN;  c.pin_d2 = CAM_Y4_PIN;
    c.pin_d1 = CAM_Y3_PIN;  c.pin_d0 = CAM_Y2_PIN;
    c.pin_vsync = CAM_VSYNC_PIN;
    c.pin_href  = CAM_HREF_PIN;
    c.pin_pclk  = CAM_PCLK_PIN;
    c.xclk_freq_hz = CAM_XCLK_HZ;
    c.ledc_timer   = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_JPEG;
    c.frame_size   = CAM_FRAME_SIZE_MAX;   // puhvrid suurima jaoks; tegelik suurus seatakse kohe pärast
    c.jpeg_quality = CAM_JPEG_QUALITY;
    c.fb_count     = CAM_FB_COUNT;
    c.fb_location  = CAMERA_FB_IN_PSRAM;
    c.grab_mode    = CAMERA_GRAB_LATEST;   // alati värskeim kaader → madal viivitus
    return c;
}

static bool initSensor() {
    camera_config_t cfg = makeConfig();
    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "esp_camera_init ebaõnnestus: 0x%x (%s)", err, esp_err_to_name(err));
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return false;

    switch (s->id.PID) {
        case OV5640_PID: s_sensor = "OV5640"; break;
        case OV2640_PID: s_sensor = "OV2640"; break;
        case OV3660_PID: s_sensor = "OV3660"; break;
        default:         s_sensor = "unknown"; break;
    }
    LOGI(TAG, "Kaamera sensor: %s (PID 0x%04x)", s_sensor, s->id.PID);
    s_maxFs = CAM_FRAME_SIZE_MAX;
    if (camera_sensor_info_t *inf = esp_camera_sensor_get_info(&s->id))
        if (inf->max_size < s_maxFs) s_maxFs = inf->max_size;

    // Mõistlikud vaikepildiseaded
    s->set_quality(s, CAM_JPEG_QUALITY);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
    }
    int fs = Settings::get().framesize;
    if (!setFramesize(fs, false)) setFramesize(CAM_FRAME_SIZE, false);
    return true;
}

// OV5640 AF püsivara laadimine. OV2640/OV3660 AF-i ei toeta.
static void initAutofocus() {
    sensor_t *s = esp_camera_sensor_get();
    s_afOk = false;
    if (!s || s->id.PID != OV5640_PID) {
        LOGW(TAG, "Sensor ei ole OV5640 – autofookus pole saadaval");
        return;
    }
    s_ov5640.start(s);
    if (s_ov5640.focusInit() != 0) {
        LOGE(TAG, "OV5640 AF püsivara laadimine ebaõnnestus");
        return;
    }
    s_afOk = true;
#if CAM_AF_CONTINUOUS
    if (s_ov5640.autoFocusMode() == 0) LOGI(TAG, "OV5640 pidev autofookus sees");
    else LOGW(TAG, "OV5640 pideva AF režiimi käivitamine ebaõnnestus");
#else
    LOGI(TAG, "OV5640 AF valmis (käsitsi 'Refocus')");
#endif
}

// Saada AF käsk (ACK=1, CMD=x). Tagastab kohe; olekut loetakse hiljem.
static void afCommand(uint8_t cmd) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    s->set_reg(s, AF_CMD_ACK, 0xFF, 0x01);
    s->set_reg(s, AF_CMD_MAIN, 0xFF, cmd);
}

// --- Pööramine ---------------------------------------------------------------
// Ainult 0° ja 180°: 180° = sensori vflip + hmirror (lisamälu ega -aega ei kulu).
// 90°/270° (tarkvaraline JPEG ümberkodeerimine) eemaldati v1.8.0-s mälu säästmiseks.
static void applySensorFlip() {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    bool flip180 = (s_rotation == 180);
    s->set_vflip(s, s_userVflip ^ flip180);
    s->set_hmirror(s, s_userHmirror ^ flip180);
}

// --- Hõivetask -------------------------------------------------------------
static void captureTask(void *) {
    const uint32_t minInterval = 1000 / CAM_MAX_FPS;
    uint32_t fails = 0, lastAfPoll = 0, afTriggerAt = 0;
    bool afPending = false;
    uint32_t fpsT0 = millis(), fpsN = 0;

    uint32_t dropUntil = 0;                     // pärast ärkamist: vanad/kohanduvad kaadrid
    s_lastUse = millis();

    for (;;) {
        uint32_t t0 = millis();

        // --- Aku säästmine: keegi ei vaata → andur ooterežiimi, hõive seisab
        bool wanted = s_consumers > 0 || t0 - s_lastUse < CAM_IDLE_SLEEP_MS;
        if (!wanted && !s_sleeping) {
            sensor_t *s = esp_camera_sensor_get();
            if (s && s->id.PID == OV5640_PID) s->set_reg(s, 0x3008, 0x40, 0x40);   // tarkvaraline ooterežiim
            s_sleeping = true;
            s_fps = 0;
            LOGI(TAG, "Vaatajaid pole – kaamera ooterežiimi (aku säästmine)");
        }
        if (s_sleeping) {
            if (!wanted) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
            sensor_t *s = esp_camera_sensor_get();
            if (s && s->id.PID == OV5640_PID) s->set_reg(s, 0x3008, 0x40, 0x00);   // ärata
            s_sleeping = false;
            dropUntil = millis() + CAM_WAKE_DROP_MS;
            fpsT0 = millis(); fpsN = 0;
#if CAM_AF_CONTINUOUS
            if (s_afOk) afCommand(AF_CONTINUOUS);
#endif
            LOGI(TAG, "Kaamera ärkas (vaataja)");
        }

        // --- AF käsud ja oleku lugemine (samast taskist → SCCB pole jagatud)
        if (s_afOk) {
            if (s_refocusReq) {
                s_refocusReq = false;
                afCommand(AF_TRIGGER);
                afPending = true;
                afTriggerAt = t0;
                LOGI(TAG, "AF: ühekordne fookus käivitatud");
            }
            if (t0 - lastAfPoll > 300) {
                lastAfPoll = t0;
                sensor_t *s = esp_camera_sensor_get();
                int st = s ? s->get_reg(s, AF_FW_STATUS, 0xFF) : -1;
                if (st >= 0) s_afRaw = (uint8_t)st;
                // Pärast ühekordset fookust naase pidevasse režiimi
                if (afPending && (s_afRaw == AF_ST_FOCUSED || t0 - afTriggerAt > 4000)) {
                    afPending = false;
                    LOGI(TAG, "AF: %s", s_afRaw == AF_ST_FOCUSED ? "fookuses" : "timeout");
#if CAM_AF_CONTINUOUS
                    afCommand(AF_CONTINUOUS);
#endif
                }
            }
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            if (++fails >= 25) {
                // Kaamera "kinni jäänud" (nt ESD, halb kontakt) → reinit
                LOGE(TAG, "Kaamera ei anna kaadreid – taaskäivitan draiveri");
                esp_camera_deinit();
                vTaskDelay(pdMS_TO_TICKS(200));
                if (initSensor()) { initAutofocus(); applySensorFlip(); }
                fails = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        fails = 0;
        if (dropUntil && (int32_t)(millis() - dropUntil) < 0) {   // ooterežiimi-eelsed / säri kohandub
            esp_camera_fb_return(fb);
            continue;
        }
        dropUntil = 0;

        const uint8_t *src = fb->buf;
        size_t srcLen = fb->len;
        uint16_t fw = fb->width, fh = fb->height;
        bool okFrame = fb->format == PIXFORMAT_JPEG && fb->len > 128;

        if (okFrame) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            if (ensureCap(s_buf, s_cap, srcLen)) {
                memcpy(s_buf, src, srcLen);
                s_len = srcLen;
                s_w = fw;
                s_h = fh;
                s_ts = millis();
                s_seq++;
                if (s_seq == 0) s_seq = 1;   // 0 tähendab "pole kaadrit"
            }
            xSemaphoreGive(s_mtx);
            fpsN++;
        }
        esp_camera_fb_return(fb);

        uint32_t now = millis();
        if (now - fpsT0 >= 2000) {
            s_fps = fpsN * 1000.0f / (now - fpsT0);
            fpsN = 0;
            fpsT0 = now;
        }

        // Tempo: ilma vaatajateta hõivame harvem (vähem soojust ja voolu),
        // kuid piisavalt tihti, et /capture annaks värske pildi ja AF töötaks.
        uint32_t interval = s_consumers > 0 ? minInterval : 250;
        uint32_t spent = millis() - t0;
        vTaskDelay(pdMS_TO_TICKS(spent < interval ? interval - spent : 1));
    }
}

// --- Avalik API ------------------------------------------------------------
bool begin() {
    if (!psramFound()) {
        LOGE(TAG, "PSRAM puudub! Kontrolli platformio.ini memory_type = qio_opi");
        return false;
    }
    s_mtx = xSemaphoreCreateMutex();
    if (!initSensor()) return false;
    initAutofocus();
    s_ready = true;
    {
        int r = Settings::get().rotation;   // 0 või 180 (vana 90°/270° teisendab Settings::load)
        s_rotation = r;
        applySensorFlip();
        if (r) LOGI(TAG, "Pildi pööre %d°", r);
    }
    // Core 1 – PPP/lwIP töötab core 0 peal. Prioriteet 2 < voo saatjad (3).
    xTaskCreatePinnedToCore(captureTask, "cam_capture", 6144, nullptr, 2, nullptr, 1);
    return true;
}

bool ready() { return s_ready; }

bool waitFrame(Frame &dst, uint32_t lastSeq, uint32_t timeoutMs) {
    if (!s_ready) return false;
    uint32_t t0 = millis();
    s_lastUse = t0;
    if (s_sleeping) {                          // ärata ja oota värsket kaadrit (mitte enne und tehtut)
        lastSeq = s_seq;
        if (timeoutMs < CAM_WAKE_DROP_MS + 1500) timeoutMs = CAM_WAKE_DROP_MS + 1500;
    }
    for (;;) {
        if (s_seq != 0 && s_seq != lastSeq) {
            bool ok = false;
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            if (ensureCap(dst.buf, dst.cap, s_len)) {
                memcpy(dst.buf, s_buf, s_len);
                dst.len = s_len;
                dst.seq = s_seq;
                dst.width = s_w;
                dst.height = s_h;
                dst.timestampMs = s_ts;
                ok = true;
            }
            xSemaphoreGive(s_mtx);
            return ok;
        }
        if (millis() - t0 >= timeoutMs) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void freeFrame(Frame &f) {
    if (f.buf) heap_caps_free(f.buf);
    f = Frame();
}

static int s_ir = -1;   // IR-filtri viigu olek (-1 = pole seadistatud)

bool control(const char *var, int val) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return false;
    int r = -1;
    if (!strcmp(var, "ir")) {
        if (CAM_IR_PIN < 0) return false;
        pinMode(CAM_IR_PIN, OUTPUT);
        digitalWrite(CAM_IR_PIN, val ? HIGH : LOW);
        s_ir = val ? 1 : 0;
        LOGI(TAG, "IR-filtri viik GPIO%d = %d", CAM_IR_PIN, s_ir);
        return true;
    }
    if      (!strcmp(var, "framesize"))  return setFramesize(val);
    else if (!strcmp(var, "quality"))    r = s->set_quality(s, val);
    else if (!strcmp(var, "brightness")) r = s->set_brightness(s, val);
    else if (!strcmp(var, "contrast"))   r = s->set_contrast(s, val);
    else if (!strcmp(var, "saturation")) r = s->set_saturation(s, val);
    else if (!strcmp(var, "sharpness"))  r = s->set_sharpness(s, val);
    else if (!strcmp(var, "aec"))        r = s->set_exposure_ctrl(s, val);
    else if (!strcmp(var, "aec2"))       r = s->set_aec2(s, val);
    else if (!strcmp(var, "ae_level"))   r = s->set_ae_level(s, val);
    else if (!strcmp(var, "aec_value"))  r = s->set_aec_value(s, val);
    else if (!strcmp(var, "agc"))        r = s->set_gain_ctrl(s, val);
    else if (!strcmp(var, "agc_gain"))   r = s->set_agc_gain(s, val);
    else if (!strcmp(var, "gainceiling"))r = s->set_gainceiling(s, (gainceiling_t)val);
    else if (!strcmp(var, "awb"))        r = s->set_whitebal(s, val);
    else if (!strcmp(var, "awb_gain"))   r = s->set_awb_gain(s, val);
    else if (!strcmp(var, "wb_mode"))    r = s->set_wb_mode(s, val);
    else if (!strcmp(var, "hmirror"))    { s_userHmirror = val; applySensorFlip(); r = 0; }
    else if (!strcmp(var, "vflip"))      { s_userVflip = val; applySensorFlip(); r = 0; }
    else if (!strcmp(var, "rotate"))     return setRotation(val);
    else if (!strcmp(var, "special_effect")) r = s->set_special_effect(s, val);
    else return false;
    LOGI(TAG, "Seade %s = %d (%s)", var, val, r == 0 ? "OK" : "viga");
    return r == 0;
}

int readReg(int reg) {
    sensor_t *s = esp_camera_sensor_get();
    return s ? s->get_reg(s, reg, 0xFF) : -1;
}

String settingsJson() {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return "{}";
    const camera_status_t &st = s->status;
    char b[512];
    snprintf(b, sizeof(b),
        "{\"framesize\":%d,\"quality\":%d,\"brightness\":%d,\"contrast\":%d,\"saturation\":%d,"
        "\"sharpness\":%d,\"aec\":%d,\"aec2\":%d,\"ae_level\":%d,\"aec_value\":%d,\"agc\":%d,"
        "\"agc_gain\":%d,\"gainceiling\":%d,\"awb\":%d,\"awb_gain\":%d,\"wb_mode\":%d,"
        "\"hmirror\":%d,\"vflip\":%d,\"special_effect\":%d,\"ir\":%d,\"ir_pin\":%d,\"rotate\":%d}",
        st.framesize, st.quality, st.brightness, st.contrast, st.saturation, st.sharpness,
        st.aec, st.aec2, st.ae_level, st.aec_value, st.agc, st.agc_gain, st.gainceiling,
        st.awb, st.awb_gain, st.wb_mode, (int)s_userHmirror, (int)s_userVflip, st.special_effect, s_ir, CAM_IR_PIN, (int)s_rotation);
    return String(b);
}

bool setRotation(int deg) {
    deg = ((deg % 360) + 360) % 360;
    if (deg != 0 && deg != 180) return false;
    s_rotation = deg;
    applySensorFlip();
    Settings::Data d = Settings::get();
    d.rotation = deg;
    Settings::save(d);
    LOGI(TAG, "Pildi pööre %d°", deg);
    return true;
}

int rotation() { return s_rotation; }

bool afSupported() { return s_afOk; }

bool refocus() {
    if (!s_afOk) return false;
    s_refocusReq = true;
    return true;
}

const char *afStatus() {
    if (!s_afOk) return "n/a";
    switch (s_afRaw) {
        case AF_ST_FOCUSED:  return "focused";
        case AF_ST_FOCUSING: return "focusing";
        case AF_ST_IDLE:     return "idle";
        case AF_ST_INIT:     return "init";
        case AF_ST_NOFW:     return "no firmware";
        case 0xFF:           return "unknown";
        default:             return "busy";
    }
}

const char *sensorName() { return s_sensor; }

const char *resolutionName() {
    static char buf[24];
    snprintf(buf, sizeof(buf), "%ux%u", s_w, s_h);
    return buf;
}

// --- Resolutsioon ---------------------------------------------------------------
static const struct { framesize_t fs; const char *name; uint16_t w, h; } FS_LIST[] = {
    {FRAMESIZE_QVGA, "QVGA", 320, 240},   {FRAMESIZE_VGA, "VGA", 640, 480},
    {FRAMESIZE_SVGA, "SVGA", 800, 600},   {FRAMESIZE_XGA, "XGA", 1024, 768},
    {FRAMESIZE_HD, "HD 720p", 1280, 720}, {FRAMESIZE_SXGA, "SXGA", 1280, 1024},
    {FRAMESIZE_UXGA, "UXGA", 1600, 1200}, {FRAMESIZE_FHD, "Full HD 1080p", 1920, 1080},
};

bool setFramesize(int fs, bool save) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return false;
    bool known = false;
    for (const auto &f : FS_LIST) if (f.fs == fs) known = true;
    if (!known || fs > s_maxFs) {
        LOGW(TAG, "Resolutsioon %d pole lubatud (sensori maksimum %d)", fs, (int)s_maxFs);
        return false;
    }
    if (s->set_framesize(s, (framesize_t)fs) != 0) {
        LOGE(TAG, "Resolutsiooni %d seadmine ebaõnnestus", fs);
        return false;
    }
    s_framesize = fs;
    for (const auto &f : FS_LIST)
        if (f.fs == fs) LOGI(TAG, "Resolutsioon %s (%ux%u)", f.name, f.w, f.h);
    if (save) {
        Settings::Data d = Settings::get();
        d.framesize = fs;
        Settings::save(d);
    }
    return true;
}

int framesize() { return s_framesize; }

String framesizesJson() {
    String j = "[";
    for (const auto &f : FS_LIST) {
        if (f.fs > s_maxFs) continue;
        char b[80];
        snprintf(b, sizeof(b), "%s{\"v\":%d,\"name\":\"%s\",\"w\":%u,\"h\":%u}",
                 j.length() > 1 ? "," : "", (int)f.fs, f.name, f.w, f.h);
        j += b;
    }
    return j + "]";
}

float fps() { return s_fps; }
size_t lastFrameBytes() { return s_len; }
void addConsumer() { s_consumers++; }
void removeConsumer() { s_consumers--; }
int consumers() { return s_consumers; }
bool sleeping() { return s_sleeping; }

}  // namespace Camera
