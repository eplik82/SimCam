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

// I²S loetakse kõrgema sagedusega (BCLK = sagedus × 64) ja tarkvara keskmistab
// selle 16 kHz-ks. Õige BCLK ja andmevorming leitakse käivitusel proovides (vt
// probe()): töötava mikrofoni puhul annab üks kanal mõõduka signaali ja teine on
// vaikne (andmeliinil 10 kΩ maandustakisti R7). Mõlemas kanalis täismahus müra
// = mikrofon ei tööta selle taktiga/vorminguga.
#define FRAME_MS     20
#define FRAME_OUT    (MIC_RATE * FRAME_MS / 1000)     // 320 diskreeti / 20 ms
#define MAX_RATE     48000
#define FRAME_IN_MAX (MAX_RATE * FRAME_MS / 1000)     // 960 stereopaari / 20 ms
#define RING         65536                            // 4,096 s (2^16 → odav modulo)
#define PER_MS       (MIC_RATE / 1000)                // 16 diskreeti millisekundis

namespace Audio {

struct I2sCfg { uint32_t rate; bool msb; const char *name; };
// 48 kHz → BCLK 3,072 MHz (lähim LilyGO näite 2,82 MHz-le), 32 kHz → 2,048 MHz,
// 16 kHz → 1,024 MHz; Philips (1-bitine viide) ja MSB (viiteta) vorming.
static const I2sCfg CFGS[] = {
    {48000, false, "48 kHz Philips"}, {48000, true, "48 kHz MSB"},
    {32000, false, "32 kHz Philips"}, {32000, true, "32 kHz MSB"},
    {16000, false, "16 kHz Philips"}, {16000, true, "16 kHz MSB"},
};
#define NCFG (sizeof(CFGS) / sizeof(CFGS[0]))

static i2s_chan_handle_t s_rx;
static TaskHandle_t      s_task;
static volatile bool     s_run;
static int16_t          *s_ring;
static volatile uint32_t s_wpos;           // kirjutatud diskreete kokku
static volatile uint32_t s_frameMs;        // millis() viimase kaadri lõpus
static volatile uint32_t s_framePos;       // s_wpos samal hetkel
static volatile float    s_gain = 1.0f;    // lineaarne
static volatile float    s_level = -90, s_peak = -90;
static volatile int      s_chanSetting = 2; // 0 = vasak, 1 = parem, 2 = automaatne
static volatile int      s_chan = 0;       // tegelikult kasutatav kanal
static volatile float    s_chanDb[2] = {-120, -120};
static volatile int      s_cfg = -1;       // valitud CFGS indeks
static int               s_autoChan = 0;   // proovimisel leitud aktiivne kanal

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool startI2s(const I2sCfg &c) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 8;
    cc.dma_frame_num = 240;                   // ≤ 4092 B DMA puhver (240 × 8 B)
    if (i2s_new_channel(&cc, nullptr, &s_rx) != ESP_OK) return false;
    i2s_std_config_t sc = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(c.rate),
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
    if (c.msb) sc.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
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

// Kanali statistika: dispersioon (dBFS 24-bit skaalal) ja nullide osakaal
// + bitimaskid diagnostikaks: millised bitid on kunagi 1 (OR) / alati 1 (AND).
// Õige I²S 24-bit andmete puhul on alumised 8 bitti (täide) alati 0.
struct ChStat { double s = 0, q = 0; uint32_t n = 0, zeros = 0, orm = 0, andm = 0xFFFFFFFF; };
static void statAdd(ChStat *st, const int32_t *in, int frames) {
    for (int i = 0; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            int32_t raw = in[2 * i + c];
            double v = raw >> 8;
            st[c].s += v; st[c].q += v * v; st[c].n++;
            st[c].orm |= (uint32_t)raw;
            st[c].andm &= (uint32_t)raw;
            if (raw == 0) st[c].zeros++;
        }
}
static float statDb(const ChStat &st) {
    if (!st.n) return -120;
    double m = st.s / st.n, var = st.q / st.n - m * m;
    return var > 1 ? 10 * log10(var) - 20 * log10(8388608.0) : -120;
}

// Proovi kõik seadistused läbi ja vali see, kus üks kanal on aktiivne ja teine vaikne
static int probe(int32_t *in) {
    int best = -1;
    float bestScore = -1e9;
    for (int k = 0; k < (int)NCFG && s_run; k++) {
        if (!startI2s(CFGS[k])) continue;
        const int frameIn = CFGS[k].rate * FRAME_MS / 1000;
        ChStat st[2];
        uint32_t t0 = millis();
        int32_t firstL = 0, firstR = 0;
        bool first = true;
        while (millis() - t0 < 600) {
            size_t got = 0;
            if (i2s_channel_read(s_rx, in, frameIn * 8, &got, 200) != ESP_OK || !got) continue;
            if (millis() - t0 < 300) continue;                  // mikrofoni käivitusaeg
            if (first) { firstL = in[20]; firstR = in[21]; first = false; }
            statAdd(st, in, got / 8);
        }
        stopI2s();
        float l = statDb(st[0]), r = statDb(st[1]);
        // Aktiivne kanal: mikrofoni müra/heli −100…−20 dBFS; vaikne: < −95 dBFS
        float act = l > r ? l : r, quiet = l > r ? r : l;
        float score = (act > -100 && act < -20 ? 50 : 0) + (quiet < -95 ? 50 : 0) - fabsf(act + 60) / 10;
        LOGI(TAG, "Proov %s: vasak %.0f dBFS, parem %.0f dBFS%s", CFGS[k].name, l, r, score >= 90 ? " ✓" : "");
        LOGI(TAG, "  bitid L: OR %08lX AND %08lX null %u%% näide %08lX | R: OR %08lX AND %08lX null %u%% näide %08lX",
             (unsigned long)st[0].orm, (unsigned long)st[0].andm, (unsigned)(st[0].n ? st[0].zeros * 100 / st[0].n : 0),
             (unsigned long)firstL, (unsigned long)st[1].orm, (unsigned long)st[1].andm,
             (unsigned)(st[1].n ? st[1].zeros * 100 / st[1].n : 0), (unsigned long)firstR);
        if (score > bestScore) { bestScore = score; best = k; s_autoChan = l > r ? 0 : 1; }
    }
    if (bestScore < 90) {
        LOGW(TAG, "Ükski seadistus ei andnud tüüpilist mikrofoni signaali – kasutan %s (vt logi)",
             CFGS[best < 0 ? 0 : best].name);
        if (best < 0) { best = 0; s_autoChan = 0; }
    } else {
        LOGI(TAG, "Valitud %s, mikrofon %s kanalis", CFGS[best].name, s_autoChan ? "paremas" : "vasakus");
    }
    return best;
}

static void task(void *) {
    int32_t *in = (int32_t *)heap_caps_malloc(FRAME_IN_MAX * 2 * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    int16_t out[FRAME_OUT];
    float dcX = 0, dcY = 0;                    // alalisvoolu eemaldus (HPF ~20 Hz)
    ChStat cst[2];
    double winSq = 0; int winN = 0; int winPeak = 0;

    if (in && s_cfg < 0) s_cfg = probe(in);   // ~4 s; tulemus logis
    const I2sCfg &cfg = CFGS[s_cfg < 0 ? 0 : s_cfg];
    if (!in || !s_run || !startI2s(cfg)) {
        if (s_run) LOGE(TAG, "I²S käivitamine ebaõnnestus");
        free(in);
        s_run = false;
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    const int frameIn = cfg.rate * FRAME_MS / 1000;
    const int dec = cfg.rate / MIC_RATE;      // 3, 2 või 1
    s_chan = s_chanSetting == 2 ? s_autoChan : s_chanSetting;
    LOGI(TAG, "Mikrofon käivitatud: %s, %s kanal, GPIO SCK=%d WS=%d SD=%d", cfg.name,
         s_chan ? "parem" : "vasak", MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN);

    while (s_run) {
        size_t got = 0;
        if (i2s_channel_read(s_rx, in, frameIn * 8, &got, 200) != ESP_OK || !got) continue;
        const int frames = got / 8;

        // Mõlema kanali tase (diagnostika seadetes)
        statAdd(cst, in, frames);
        if (cst[0].n >= cfg.rate / 2) {
            s_chanDb[0] = statDb(cst[0]);
            s_chanDb[1] = statDb(cst[1]);
            cst[0] = ChStat(); cst[1] = ChStat();
        }

        const float g = s_gain / 256.0f;      // 24 → 16 bitti × võimendus
        const int ch = s_chan;
        int n = 0;
        for (int i = 0; i + dec <= frames && n < FRAME_OUT; i += dec) {
            float x = 0;
            for (int k = 0; k < dec; k++) x += in[2 * (i + k) + ch] >> 8;
            x /= dec;
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
    s_chanSetting = d.micChan;
    if (s_task) s_chan = d.micChan == 2 ? s_autoChan : d.micChan;
    if (d.micEnabled && s_task && !s_run) {    // peatumine veel pooleli – oota
        for (int i = 0; i < 50 && s_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (d.micEnabled && !s_task) {
        s_cfg = -1;                            // iga sisselülitamine proovib seadistused läbi
        if (!s_ring) s_ring = (int16_t *)heap_caps_calloc(RING, sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!s_ring) { LOGE(TAG, "Helipuhvrile ei jätkunud mälu"); return; }
        s_run = true;
        if (xTaskCreatePinnedToCore(task, "mic", 6144, nullptr, 5, &s_task, 0) != pdPASS) {
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
const char *config() { return s_cfg >= 0 ? CFGS[s_cfg].name : "proovin…"; }
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
