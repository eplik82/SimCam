// =============================================================================
//  Mikrofon – teostus
// =============================================================================
#include "audio.h"
#include "config.h"
#include "settings.h"
#include "log.h"

#include <math.h>
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MIC";

// I²S loeb 32 kHz-ga (BCLK 2,048 MHz – MEMS mikrofoni lubatud vahemikus) ja
// tarkvara teeb sellest 16 kHz (kahe diskreedi keskmine = lihtne madalpääs).
#define I2S_RATE     (MIC_RATE * 2)
#define FRAME_MS     20
#define FRAME_IN     (I2S_RATE * FRAME_MS / 1000)     // 640 stereopaari / 20 ms
#define FRAME_OUT    (MIC_RATE * FRAME_MS / 1000)     // 320 diskreeti / 20 ms
#define RING         65536                            // 4,096 s (2^16 → odav modulo)
#define PER_MS       (MIC_RATE / 1000)                // 16 diskreeti millisekundis

namespace Audio {

static i2s_chan_handle_t s_rx;
static TaskHandle_t      s_task;
static volatile bool     s_run;
static int16_t          *s_ring;
static volatile uint32_t s_wpos;           // kirjutatud diskreete kokku
static volatile uint32_t s_frameMs;        // millis() viimase kaadri lõpus
static volatile uint32_t s_framePos;       // s_wpos samal hetkel
static volatile float    s_gain = 1.0f;    // lineaarne
static volatile float    s_level = -90, s_peak = -90;
static volatile int      s_chan = 0;       // 0 = vasak, 1 = parem (seadetest)
static volatile float    s_chanDb[2] = {-120, -120};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool startI2s() {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 6;
    cc.dma_frame_num = FRAME_IN / 2;
    if (i2s_new_channel(&cc, nullptr, &s_rx) != ESP_OK) return false;
    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)MIC_SCK_PIN,
            .ws   = (gpio_num_t)MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)MIC_SD_PIN,
            .invert_flags = {},
        },
    };
    if (i2s_channel_init_std_mode(s_rx, &sc) != ESP_OK || i2s_channel_enable(s_rx) != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return false;
    }
    return true;
}

static void stopI2s() {
    if (!s_rx) return;
    i2s_channel_disable(s_rx);
    i2s_del_channel(s_rx);
    s_rx = nullptr;
}

static void task(void *) {
    int32_t *in = (int32_t *)heap_caps_malloc(FRAME_IN * 2 * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    int16_t out[FRAME_OUT];
    float dcX = 0, dcY = 0;                    // alalisvoolu eemaldus (HPF ~20 Hz)
    // Kanalite diagnostika: 24-bit toorväärtuste dispersioon ~0,5 s aknas
    double cs[2] = {0, 0}, cq[2] = {0, 0};
    int cn = 0;
    bool reported = false;
    double winSq = 0; int winN = 0; int winPeak = 0;

    if (!in || !startI2s()) {
        LOGE(TAG, "I²S käivitamine ebaõnnestus");
        free(in);
        s_run = false;
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    LOGI(TAG, "Mikrofon käivitatud: %d Hz, GPIO SCK=%d WS=%d SD=%d", MIC_RATE, MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN);

    while (s_run) {
        size_t got = 0;
        if (i2s_channel_read(s_rx, in, FRAME_IN * 2 * sizeof(int32_t), &got, 200) != ESP_OK || !got) continue;
        const int frames = got / (2 * sizeof(int32_t));

        // Mõlema kanali tase (dispersioon → dBFS 24-bit skaalal)
        for (int i = 0; i < frames; i++)
            for (int c = 0; c < 2; c++) {
                double v = in[2 * i + c] >> 8;
                cs[c] += v; cq[c] += v * v;
            }
        cn += frames;
        if (cn >= I2S_RATE / 2) {
            for (int c = 0; c < 2; c++) {
                double m = cs[c] / cn, var = cq[c] / cn - m * m;
                s_chanDb[c] = var > 1 ? 10 * log10(var) - 20 * log10(8388608.0) : -120;
                cs[c] = cq[c] = 0;
            }
            cn = 0;
            if (!reported) {
                reported = true;
                LOGI(TAG, "Kanalite tase: vasak %.0f dBFS, parem %.0f dBFS – kasutan %s kanalit",
                     s_chanDb[0], s_chanDb[1], s_chan ? "paremat" : "vasakut");
                if (s_chanDb[s_chan] < -110)
                    LOGW(TAG, "Valitud kanalis pole signaali – proovi seadetes teist kanalit");
            }
        }

        const float g = s_gain / 256.0f;      // 24 → 16 bitti × võimendus
        const int ch = s_chan;
        int n = 0;
        for (int i = 0; i + 1 < frames && n < FRAME_OUT; i += 2) {
            float x = ((in[2 * i + ch] >> 8) + (in[2 * (i + 1) + ch] >> 8)) * 0.5f;
            float y = x - dcX + 0.996f * dcY;  // 1. järku kõrgpääs
            dcX = x; dcY = y;
            float v = y * g;
            int s = v > 32767 ? 32767 : (v < -32768 ? -32768 : (int)v);
            out[n++] = s;
            winSq += (double)s * s;
            if (abs(s) > winPeak) winPeak = abs(s);
        }
        winN += n;

        uint32_t w = s_wpos;
        for (int i = 0; i < n; i++) s_ring[(w + i) & (RING - 1)] = out[i];
        portENTER_CRITICAL(&s_mux);
        s_wpos = w + n;
        s_framePos = w + n;
        s_frameMs = millis();
        portEXIT_CRITICAL(&s_mux);

        if (winN >= MIC_RATE / 5) {           // 0,2 s aken
            float rms = sqrt(winSq / winN);
            s_level = rms > 1 ? 20 * log10f(rms / 32768.0f) : -90;
            s_peak = winPeak > 1 ? 20 * log10f(winPeak / 32768.0f) : -90;
            winSq = 0; winN = 0; winPeak = 0;
        }
    }

    stopI2s();
    free(in);
    s_level = -90;
    s_peak = -90;
    s_chanDb[0] = -120;
    s_chanDb[1] = -120;
    LOGI(TAG, "Mikrofon peatatud");
    s_task = nullptr;
    vTaskDelete(nullptr);
}

void apply() {
    const Settings::Data d = Settings::get();
    s_gain = powf(10.0f, d.micGain / 20.0f);
    s_chan = d.micChan;
    if (d.micEnabled && s_task && !s_run) {    // peatumine veel pooleli – oota
        for (int i = 0; i < 50 && s_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (d.micEnabled && !s_task) {
        if (!s_ring) s_ring = (int16_t *)heap_caps_calloc(RING, sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!s_ring) { LOGE(TAG, "Helipuhvrile ei jätkunud mälu"); return; }
        s_run = true;
        if (xTaskCreatePinnedToCore(task, "mic", 4096, nullptr, 5, &s_task, 0) != pdPASS) {
            s_run = false;
            s_task = nullptr;
        }
    } else if (!d.micEnabled && s_task) {
        s_run = false;                         // task peatab I²S-i ja lõpetab ise
    }
}

void begin() {
    apply();
    if (!Settings::get().micEnabled) LOGI(TAG, "Mikrofon on seadetes välja lülitatud");
}

bool running() { return s_task != nullptr && s_run; }
uint32_t position() { return s_wpos; }
int channel() { return s_chan; }
float chanDb(int ch) { return s_chanDb[ch & 1]; }
float levelDb() { return s_level; }
float peakDb() { return s_peak; }

uint32_t msAt(uint32_t pos) {
    portENTER_CRITICAL(&s_mux);
    uint32_t fp = s_framePos, fm = s_frameMs;
    portEXIT_CRITICAL(&s_mux);
    return fm - (int32_t)(fp - pos) / PER_MS;
}

bool read(uint32_t *pos, int16_t *dst, size_t n, uint32_t maxLagMs, uint32_t *skipped) {
    if (skipped) *skipped = 0;
    if (!running() || !s_ring) return false;
    uint32_t w = s_wpos;
    int32_t lag = (int32_t)(w - *pos);
    if (lag < 0 || lag > (int32_t)(RING - 2 * FRAME_OUT) || (uint32_t)lag > maxLagMs * PER_MS + n) {
        uint32_t np = w - (lag < 0 ? 0 : (n < (size_t)lag ? n : lag));   // hüppa uusima juurde
        if (skipped && lag > 0) *skipped = np - *pos;
        *pos = np;
        lag = (int32_t)(w - *pos);
    }
    if ((size_t)lag < n) return false;
    for (size_t i = 0; i < n; i++) dst[i] = s_ring[(*pos + i) & (RING - 1)];
    *pos += n;
    return true;
}

uint8_t mulaw(int16_t sample) {
    const int BIAS = 0x84, CLIP = 32635;
    int s = sample;
    int sign = 0;
    if (s < 0) { s = -s; sign = 0x80; }
    if (s > CLIP) s = CLIP;
    s += BIAS;
    int exp = 7;
    for (int mask = 0x4000; (s & mask) == 0 && exp > 0; exp--, mask >>= 1) {}
    int mant = (s >> (exp + 3)) & 0x0F;
    return ~(sign | (exp << 4) | mant);
}

size_t encodePcmu(const int16_t *in16k, size_t n, uint8_t *out) {
    size_t m = n / 2;
    for (size_t i = 0; i < m; i++) out[i] = mulaw((in16k[2 * i] + in16k[2 * i + 1]) / 2);
    return m;
}

}  // namespace Audio
