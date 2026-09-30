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
#include "esp_jpeg_dec.h"      // esp_new_jpeg: SIMD-kiirendatud JPEG dekooder/kooder
#include "esp_jpeg_enc.h"
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
static volatile uint8_t  s_afRaw = 0xFF;
static const char       *s_sensor = "unknown";

// --- Pööramine -------------------------------------------------------------
static volatile int      s_rotation = 0;        // 0/90/180/270
static volatile bool     s_rot90 = false;       // kas tarkvaraline 90° pööre
static bool              s_userVflip = false, s_userHmirror = false;
static volatile float    s_rotMs = 0;

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
    c.frame_size   = CAM_FRAME_SIZE;
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
// 180° = sensori vflip + hmirror (tasuta). 90° = tarkvaraline ümberkodeerimine.
// 270° = sensori 180° + tarkvaraline 90°.
static void applySensorFlip() {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    bool flip180 = (s_rotation == 180 || s_rotation == 270);
    s->set_vflip(s, s_userVflip ^ flip180);
    s->set_hmirror(s, s_userHmirror ^ flip180);
}

static jpeg_dec_handle_t s_dec = nullptr;
static jpeg_enc_handle_t s_enc = nullptr;
static int      s_encW = 0, s_encH = 0;
static uint8_t *s_raw = nullptr;  static int s_rawCap = 0;
static uint8_t *s_jpg = nullptr;  static int s_jpgCap = 0;

// Dekodeeri + pööra 90° päripäeva + kodeeri. Tulemus s_jpg-s.
static bool rotateJpeg90(const uint8_t *in, size_t inLen, int &outLen, uint16_t &w, uint16_t &h) {
    if (!s_dec) {
        jpeg_dec_config_t dc = DEFAULT_JPEG_DEC_CONFIG();
        dc.output_type = JPEG_PIXEL_FORMAT_CbYCrY;           // YUV422, 2 B/px
        dc.rotate = JPEG_ROTATE_90D;
        if (jpeg_dec_open(&dc, &s_dec) != JPEG_ERR_OK) { LOGE(TAG, "rotate: dec_open"); s_dec = nullptr; return false; }
    }
    jpeg_dec_io_t io = {};
    io.inbuf = (uint8_t *)in;
    io.inbuf_len = inLen;
    jpeg_dec_header_info_t info = {};
    jpeg_error_t he = jpeg_dec_parse_header(s_dec, &io, &info);
    if (he != JPEG_ERR_OK) { LOGE(TAG, "rotate: parse_header %d", he); return false; }
    int need = 0;
    if (jpeg_dec_get_outbuf_len(s_dec, &need) != JPEG_ERR_OK || need <= 0) {
        LOGE(TAG, "rotate: outbuf_len %d", need);
        return false;
    }
    if (need > s_rawCap) {
        // ~1 MB (800x600x2) – ainult PSRAM-i mahub; 16-baidine joondus (SIMD)
        if (s_raw) heap_caps_free(s_raw);
        s_raw = (uint8_t *)heap_caps_aligned_alloc(16, need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_rawCap = s_raw ? need : 0;
        if (!s_raw) { LOGE(TAG, "rotate: PSRAM %d B eraldamine ebaõnnestus", need); return false; }
    }
    io.outbuf = s_raw;
    jpeg_error_t de = jpeg_dec_process(s_dec, &io);
    if (de != JPEG_ERR_OK) { LOGE(TAG, "rotate: dec_process %d (%ux%u, need %d)", de, info.width, info.height, need); return false; }

    // Pööramisega dekooder võib päises anda juba pööratud mõõtmed – võrdle
    // kaamera algsete mõõtmetega (w, h), et väljund oleks alati h x w.
    const int ow = (info.width == w) ? info.height : info.width;
    const int oh = (info.width == w) ? info.width : info.height;
    if (ow * oh * 2 > need) { LOGE(TAG, "rotate: puhver %d < %d", need, ow * oh * 2); return false; }
    if (!s_enc || ow != s_encW || oh != s_encH) {
        if (s_enc) jpeg_enc_close(s_enc);
        jpeg_enc_config_t ec = DEFAULT_JPEG_ENC_CONFIG();
        ec.width = ow;
        ec.height = oh;
        ec.src_type = JPEG_PIXEL_FORMAT_CbYCrY;
        ec.subsampling = JPEG_SUBSAMPLE_422;                  // RTP/JPEG tüüp 0
        ec.quality = CAM_ROTATE_QUALITY;
        jpeg_error_t oe = jpeg_enc_open(&ec, &s_enc);
        if (oe != JPEG_ERR_OK) { LOGE(TAG, "rotate: enc_open %d (%dx%d)", oe, ow, oh); s_enc = nullptr; return false; }
        s_encW = ow; s_encH = oh;
        int cap = ow * oh / 2 + 16384;                        // piisav ka detailse pildi jaoks
        if (s_jpg) heap_caps_free(s_jpg);
        s_jpg = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_jpgCap = s_jpg ? cap : 0;
        if (!s_jpg) { LOGE(TAG, "rotate: väljundpuhvri eraldamine ebaõnnestus"); return false; }
    }
    jpeg_error_t ee = jpeg_enc_process(s_enc, s_raw, ow * oh * 2, s_jpg, s_jpgCap, &outLen);
    if (ee != JPEG_ERR_OK) { LOGE(TAG, "rotate: enc_process %d (%dx%d, need %d, cap %d)", ee, ow, oh, need, s_jpgCap); return false; }
    w = ow; h = oh;
    return true;
}

// --- Hõivetask -------------------------------------------------------------
static void captureTask(void *) {
    const uint32_t minInterval = 1000 / CAM_MAX_FPS;
    uint32_t fails = 0, lastAfPoll = 0, afTriggerAt = 0;
    bool afPending = false;
    uint32_t fpsT0 = millis(), fpsN = 0;

    for (;;) {
        uint32_t t0 = millis();

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

        const uint8_t *src = fb->buf;
        size_t srcLen = fb->len;
        uint16_t fw = fb->width, fh = fb->height;
        bool okFrame = fb->format == PIXFORMAT_JPEG && fb->len > 128;
        if (okFrame && s_rot90) {
            uint32_t r0 = millis();
            int outLen = 0;
            okFrame = rotateJpeg90(fb->buf, fb->len, outLen, fw, fh);
            if (okFrame) { src = s_jpg; srcLen = outLen; }
            s_rotMs = s_rotMs * 0.9f + (millis() - r0) * 0.1f;
        }

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
        int r = Settings::get().rotation;
        s_rotation = r;
        s_rot90 = (r == 90 || r == 270);
        applySensorFlip();
        if (r) LOGI(TAG, "Pildi pööre %d°", r);
    }
    // Core 1 – PPP/lwIP töötab core 0 peal. Prioriteet 2 < voo saatjad (3):
    // 90°/270° ümberkodeerimine (~260 ms/kaader) hõivab muidu kogu tuuma ja
    // MJPEG/RTSP saatjad jäävad protsessoriajast ilma (vaataja sai ~0,5 fps).
    xTaskCreatePinnedToCore(captureTask, "cam_capture", 12288, nullptr, 2, nullptr, 1);
    return true;
}

bool ready() { return s_ready; }

bool waitFrame(Frame &dst, uint32_t lastSeq, uint32_t timeoutMs) {
    if (!s_ready) return false;
    uint32_t t0 = millis();
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
    if      (!strcmp(var, "framesize"))  { if (val < 0 || val >= FRAMESIZE_INVALID) return false; r = s->set_framesize(s, (framesize_t)val); }
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
    if (deg % 90) return false;
    s_rotation = deg;
    s_rot90 = (deg == 90 || deg == 270);
    applySensorFlip();
    Settings::Data d = Settings::get();
    d.rotation = deg;
    Settings::save(d);
    LOGI(TAG, "Pildi pööre %d°%s", deg, s_rot90 ? " (tarkvaraline ümberkodeerimine)" : "");
    return true;
}

int rotation() { return s_rotation; }
float rotateMs() { return s_rot90 ? s_rotMs : 0; }

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

float fps() { return s_fps; }
size_t lastFrameBytes() { return s_len; }
void addConsumer() { s_consumers++; }
void removeConsumer() { s_consumers--; }
int consumers() { return s_consumers; }

}  // namespace Camera
