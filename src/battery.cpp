// =============================================================================
//  Aku – teostus
// =============================================================================
#include "battery.h"
#include "config.h"
#include "settings.h"
#include "log.h"

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BAT";

#define SAMPLE_MS      2000       // mõõtmise samm
#define SAMPLES        16         // ADC lugemisi ühe mõõtmise kohta (keskmistatakse)
#define SLOPE_MIN      10         // oleku jaoks: pinge muutus viimase 10 min jooksul
#define EST_WINDOW_MIN 60         // tööaja hinnang: täituvuse muutus viimase 60 min jooksul
#define EST_MIN_MIN    20         // … kuid vähemalt 20 min andmeid
#define V_ABSENT       2.50f      // sellest madalam → akut pole
#define V_FULL         4.15f

namespace Battery {

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Status    s_st = {};
static float     s_raw = 0;              // filtreeritud V (ilma kalibreerimiseta)
static uint16_t *s_hist;                 // ringpuhver, mV (kalibreeritud)
static uint32_t  s_histN;                // lisatud punkte kokku
static uint32_t  s_chargeMark;           // ajaloo indeks, kus viimati laeti
static int       s_pctShown = -1;        // näidatav % (tühjenemisel ainult langeb)
static uint8_t   s_warned;               // 1 = madal hoiatus antud, 2 = kriitiline

// Li-ion rakk kaamera koormuse all (~0,1 C): pinge → täituvus %.
// Laadija lõpetab 4,20 V juures, kuid koormuse all langeb täis aku pinge kohe
// ~4,15 V-ni – seepärast on 100 % juba 4,15 V juures.
static const struct { float v; int p; } CURVE[] = {
    {4.15f, 100}, {4.06f, 90}, {3.97f, 80}, {3.90f, 70}, {3.83f, 60}, {3.77f, 50},
    {3.73f, 40}, {3.69f, 30}, {3.65f, 20}, {3.59f, 10}, {3.48f, 5}, {3.30f, 0},
};

static float percentF(float v) {
    const int n = sizeof(CURVE) / sizeof(CURVE[0]);
    if (v >= CURVE[0].v) return 100;
    if (v <= CURVE[n - 1].v) return 0;
    for (int i = 1; i < n; i++)
        if (v >= CURVE[i].v) {
            float f = (v - CURVE[i].v) / (CURVE[i - 1].v - CURVE[i].v);
            return CURVE[i].p + f * (CURVE[i - 1].p - CURVE[i].p);
        }
    return 0;
}

static int percentFor(float v) {
    const int n = sizeof(CURVE) / sizeof(CURVE[0]);
    if (v >= CURVE[0].v) return 100;
    if (v <= CURVE[n - 1].v) return 0;
    for (int i = 1; i < n; i++)
        if (v >= CURVE[i].v) {
            float f = (v - CURVE[i].v) / (CURVE[i - 1].v - CURVE[i].v);
            return (int)lroundf(CURVE[i].p + f * (CURVE[i - 1].p - CURVE[i].p));
        }
    return 0;
}

static float readAdcV() {
    uint32_t sum = 0;
    uint16_t mn = 0xFFFF, mx = 0;
    for (int i = 0; i < SAMPLES; i++) {
        uint16_t mv = analogReadMilliVolts(BAT_ADC_PIN);
        sum += mv;
        if (mv < mn) mn = mv;
        if (mv > mx) mx = mv;
    }
    // jäta välja suurim ja väikseim (WiFi saatmise ajal tekkivad piigid)
    return (sum - mn - mx) / (float)(SAMPLES - 2) / 1000.0f * BAT_DIVIDER;
}

// Muutus minutis viimase `minutes` jooksul – vähimruutude sirge.
// pct = false → mV/min, pct = true → %/min (täituvuse kõvera järgi; arvestab, et
// pinge on aku keskosas peaaegu paigal ja langeb lõpus järsult).
static bool slopeOver(int minutes, float *out, bool pct = false, int minMinutes = 3,
                      uint32_t since = 0) {
    int pts = minutes * 60 / BAT_HIST_SEC;
    uint32_t have = s_histN < BAT_HIST_LEN ? s_histN : BAT_HIST_LEN;
    if (s_histN - since < have) have = s_histN - since;      // ainult punktid alates `since`
    if ((int)have < pts) pts = have;
    if (pts < minMinutes * 60 / BAT_HIST_SEC) return false;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < pts; i++) {
        double x = i * (BAT_HIST_SEC / 60.0);      // minutid
        double mv = s_hist[(s_histN - pts + i) % BAT_HIST_LEN];
        double y = pct ? percentF(mv / 1000.0f) : mv;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double d = pts * sxx - sx * sx;
    if (d <= 0) return false;
    *out = (float)((pts * sxy - sx * sy) / d);
    return true;
}

static int histMinutes() {
    uint32_t have = s_histN < BAT_HIST_LEN ? s_histN : BAT_HIST_LEN;
    return have * BAT_HIST_SEC / 60;
}

static void task(void *) {
    uint32_t lastHist = 0;
    for (;;) {
        const Settings::Data cfg = Settings::get();
        float v = readAdcV();
        s_raw = s_raw == 0 ? v : s_raw * 0.8f + v * 0.2f;       // EMA, ~10 s
        const float cal = s_raw * cfg.batCal;

        if (s_hist && (s_histN == 0 || millis() - lastHist >= BAT_HIST_SEC * 1000UL)) {
            lastHist = millis();
            s_hist[s_histN % BAT_HIST_LEN] = (uint16_t)lroundf(cal * 1000.0f);
            s_histN++;
        }

        Status st = {};
        st.enabled = cfg.batEnabled;
        st.voltage = cal;
        st.raw = s_raw;
        st.percent = percentFor(cal);
        st.minutesLeft = -1;
        st.usbHost = HWCDC::isPlugged();
        st.histMinutes = histMinutes();
        float slope = 0, rate = 0;
        bool haveSlope = slopeOver(SLOPE_MIN, &slope);
        // Täituvuse muutus kuni 60 min aknas (vähemalt EST_MIN_MIN min andmeid)
        // – ainult pärast viimast laadimist, et tõus ei segaks tühjenemise kiirust
        bool haveRate = slopeOver(EST_WINDOW_MIN, &rate, true, EST_MIN_MIN, s_chargeMark);
        st.slope = haveSlope ? slope : 0;
        const bool draining = haveRate && rate <= -0.02f;          // ≥ 1,2 %/h

        if (cal < V_ABSENT)                         st.state = State::Absent;
        else if (haveSlope && slope >= 1.5f)        st.state = State::Charging;
        else if (draining || (haveSlope && slope <= -0.3f))
            st.state = st.percent <= 15 ? State::Low : State::Discharging;
        else if (cal >= V_FULL)                     st.state = State::Full;
        else if (!haveSlope)                        st.state = State::Measuring;
        else                                        st.state = State::Stable;

        if (st.state == State::Charging) s_chargeMark = s_histN;

        // Täituvus tühjenemisel ei tõuse (koormuse muutus tõstab pinget hetkeks)
        if (st.state == State::Charging || st.state == State::Full || st.state == State::Absent ||
            s_pctShown < 0 || st.percent < s_pctShown)
            s_pctShown = st.percent;
        st.percent = s_pctShown;

        // Tööaeg: allesolev % / kulu %/min. Laadimisel ja laadijal hinnangut pole.
        if (draining && st.state != State::Charging) {
            float m = st.percent / -rate;
            st.minutesLeft = m > 99 * 60 ? 99 * 60 : (int)m;
        }

        // Hoiatused logisse (üks kord tühjenemise kohta)
        if (cfg.batEnabled && st.state != State::Absent) {
            if (st.state == State::Charging || st.state == State::Full) s_warned = 0;
            else if (st.percent <= 5 && s_warned < 2 && haveSlope && slope < 0) {
                s_warned = 2;
                LOGE(TAG, "Aku peaaegu tühi: %.2f V (%d %%) – seade lülitub peagi välja", cal, st.percent);
            } else if (st.percent <= 15 && s_warned < 1 && haveSlope && slope < 0) {
                s_warned = 1;
                LOGW(TAG, "Aku madal: %.2f V (%d %%)", cal, st.percent);
            }
        }

        portENTER_CRITICAL(&s_mux);
        State prev = s_st.state;
        s_st = st;
        portEXIT_CRITICAL(&s_mux);
        if (prev != st.state && cfg.batEnabled && st.state != State::Measuring)
            LOGI(TAG, "Aku: %s, %.2f V, %d %%, %+.1f mV/min", stateName(st.state), cal, st.percent, st.slope);

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

void begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);   // kuni ~3,1 V → aku kuni ~6 V
    s_hist = (uint16_t *)heap_caps_malloc(BAT_HIST_LEN * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_raw = readAdcV();
    s_st.state = State::Measuring;
    xTaskCreatePinnedToCore(task, "battery", 4096, nullptr, 1, nullptr, 0);
    const Settings::Data cfg = Settings::get();
    LOGI(TAG, "Aku pinge %.2f V (GPIO%d, kalibreering ×%.3f)%s", s_raw * cfg.batCal, BAT_ADC_PIN,
         cfg.batCal, cfg.batEnabled ? "" : " – aku jälgimine seadetes väljas");
}

Status status() {
    portENTER_CRITICAL(&s_mux);
    Status s = s_st;
    portEXIT_CRITICAL(&s_mux);
    return s;
}

const char *stateName(State s) {
    switch (s) {
        case State::Measuring:   return "MEASURING";
        case State::Absent:      return "ABSENT";
        case State::Charging:    return "CHARGING";
        case State::Full:        return "FULL";
        case State::Discharging: return "DISCHARGING";
        case State::Low:         return "LOW";
        case State::Stable:      return "STABLE";
    }
    return "?";
}

size_t history(uint16_t *dst, size_t max) {
    if (!s_hist) return 0;
    uint32_t n = s_histN;                       // lugemine ilma lukuta: halvimal juhul
    uint32_t have = n < BAT_HIST_LEN ? n : BAT_HIST_LEN;   // üks punkt on värskem
    if (have > max) have = max;
    for (uint32_t i = 0; i < have; i++) dst[i] = s_hist[(n - have + i) % BAT_HIST_LEN];
    return have;
}

float calibrate(float measuredV) {
    if (s_raw < V_ABSENT || measuredV < 2.5f || measuredV > 4.5f) return 0;
    float f = measuredV / s_raw;
    if (!(f > 0.8f && f < 1.25f)) return 0;
    Settings::Data d = Settings::get();
    const float old = d.batCal;
    d.batCal = f;
    if (!Settings::save(d)) return 0;
    s_pctShown = -1;
    if (s_hist)                                 // ajalugu uue teguriga, et tõus/langus ei hüppaks
        for (uint32_t i = 0; i < BAT_HIST_LEN; i++) s_hist[i] = (uint16_t)lroundf(s_hist[i] * f / old);
    LOGI(TAG, "Kalibreeritud: mõõdetud %.3f V, ADC %.3f V → tegur %.4f", measuredV, s_raw, f);
    return f;
}

}  // namespace Battery
