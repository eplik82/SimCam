// =============================================================================
//  Logimine – teostus (vt log.h)
// =============================================================================
#include "log.h"

#include <stdarg.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace Log {

// --- Eelmise käivituse logi: RTC mälu (ei kustu tarkvaralisel taaskäivitusel) --
#define RTC_MAGIC 0x53434C47u   // "SCLG"
struct RtcLog {
    uint32_t magic;
    uint32_t total;             // kirjutatud baite kokku (positsioon = total % size)
    uint32_t check;             // magic ^ total ^ RTC_CHECK – kehtivuse kontroll
    char     buf[LOG_RTC_SIZE];
};
#define RTC_CHECK 0x5A5AA5A5u
RTC_NOINIT_ATTR static RtcLog s_rtc;

static SemaphoreHandle_t s_mtx;
static char    *s_buf;                 // ringpuhver (PSRAM)
static uint32_t s_total;               // kirjutatud baite kokku
static uint32_t s_base;                // clear() järel nähtava logi algus
static char    *s_prev;                // eelmise käivituse logi (lineaarne)
static size_t   s_prevLen;
static char     s_line[512];           // vormindamine (mutexi all)
static char     s_idf[256];
static char     s_ser[514];            // USB väljundi koopia (s_serMtx all)
static SemaphoreHandle_t s_serMtx;
static uint32_t s_warn, s_err;
static vprintf_like_t s_idfPrev;
static const char *s_reset = "-";

static const char *s_resetCode = "-";

static const char *resetCode(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        default:                return "OTHER";
    }
}

static const char *resetName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "toide sisse";
        case ESP_RST_EXT:       return "väline reset";
        case ESP_RST_SW:        return "tarkvaraline (taaskäivitus/uuendus)";
        case ESP_RST_PANIC:     return "KOKKUJOOKSMINE (panic)";
        case ESP_RST_INT_WDT:   return "KOKKUJOOKSMINE (katkestuse watchdog)";
        case ESP_RST_TASK_WDT:  return "KOKKUJOOKSMINE (taski watchdog)";
        case ESP_RST_WDT:       return "KOKKUJOOKSMINE (watchdog)";
        case ESP_RST_DEEPSLEEP: return "ärkamine süvaunest";
        case ESP_RST_BROWNOUT:  return "TOITEPINGE LANGUS (brownout)";
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_USB:       return "USB";
        case ESP_RST_JTAG:      return "JTAG";
        default:                return "teadmata";
    }
}

static inline bool lock(uint32_t ms = 200) {
    return s_mtx && !xPortInIsrContext() &&
           xSemaphoreTakeRecursive(s_mtx, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static inline void unlock() { xSemaphoreGiveRecursive(s_mtx); }

// Lisa tekst mõlemasse ringpuhvrisse (mutex peab olema võetud)
static void append(const char *s, size_t n) {
    if (s_buf) {
        for (size_t i = 0; i < n; i++) s_buf[(s_total + i) % LOG_BUF_SIZE] = s[i];
    }
    s_total += n;
    for (size_t i = 0; i < n; i++) s_rtc.buf[(s_rtc.total + i) % LOG_RTC_SIZE] = s[i];
    s_rtc.total += n;
    s_rtc.check = RTC_MAGIC ^ s_rtc.total ^ RTC_CHECK;
}

static void count(char lvl) {
    if (lvl == 'W') s_warn++;
    else if (lvl == 'E') s_err++;
}

// ESP-IDF logi (ESP_LOGx): prindi nagu enne ja jäta hoiatused/vead puhvrisse.
// Rida on kujul "W (12345) tag: tekst\n", võib-olla ANSI värvikoodidega.
static int idfVprintf(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int r = s_idfPrev ? s_idfPrev(fmt, ap) : vprintf(fmt, ap);
    if (lock(20)) {                            // ära pidurda WiFi/PPP taske
        vsnprintf(s_idf, sizeof(s_idf), fmt, ap2);
        const char *p = s_idf;
        if (*p == '\033') { while (*p && *p != 'm') p++; if (*p) p++; }   // värvikood
        char lvl = *p;
        if ((lvl == 'W' || lvl == 'E') && p[1] == ' ' && p[2] == '(') {
            const char *msg = strchr(p, ')');
            msg = msg ? msg + 1 : p;
            while (*msg == ' ') msg++;
            int n = snprintf(s_line, sizeof(s_line), "[%9lu][%c][IDF] ",
                             (unsigned long)millis(), lvl);
            append(s_line, n);
            size_t len = strcspn(msg, "\033\r\n");
            append(msg, len);
            append("\n", 1);
            count(lvl);
        }
        unlock();
    }
    va_end(ap2);
    return r;
}

void begin() {
    if (s_mtx) return;
    s_reset = resetName(esp_reset_reason());
    s_resetCode = resetCode(esp_reset_reason());

    // Eelmise käivituse logi RTC mälust (kehtib ainult, kui toide ei kadunud)
    if (s_rtc.magic == RTC_MAGIC && s_rtc.check == (RTC_MAGIC ^ s_rtc.total ^ RTC_CHECK) &&
        s_rtc.total > 0) {
        size_t n = s_rtc.total < LOG_RTC_SIZE ? s_rtc.total : LOG_RTC_SIZE;
        s_prev = (char *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_prev) s_prev = (char *)malloc(n + 1);
        if (s_prev) {
            uint32_t start = s_rtc.total - n;
            for (size_t i = 0; i < n; i++) {
                char c = s_rtc.buf[(start + i) % LOG_RTC_SIZE];
                s_prev[i] = (c == '\n' || c == '\t' || (uint8_t)c >= 0x20) ? c : '?';
            }
            s_prev[n] = 0;
            s_prevLen = n;
            if (n == LOG_RTC_SIZE) {                // lõika katkine esimene rida ära
                char *nl = (char *)memchr(s_prev, '\n', n);
                if (nl) { s_prevLen = n - (nl + 1 - s_prev); memmove(s_prev, nl + 1, s_prevLen + 1); }
            }
        }
    }
    s_rtc.magic = RTC_MAGIC;
    s_rtc.total = 0;
    s_rtc.check = RTC_MAGIC ^ RTC_CHECK;

    s_buf = (char *)heap_caps_malloc(LOG_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_mtx = xSemaphoreCreateRecursiveMutex();
    s_serMtx = xSemaphoreCreateMutex();
    s_idfPrev = esp_log_set_vprintf(idfVprintf);

    LOGI("LOG", "Taaskäivituse põhjus: %s%s", s_reset,
         s_prevLen ? " – eelmise käivituse logi on alles (/log)" : "");
    if (!s_buf) LOGE("LOG", "Logipuhvrile ei jätkunud PSRAM-i – veebis näeb ainult viimast %u B",
                     (unsigned)LOG_RTC_SIZE);
}

void write(const char *lvl, const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (lock()) {
        int n = snprintf(s_line, sizeof(s_line), "[%9lu][%s][%s] ", (unsigned long)millis(), lvl, tag);
        int m = vsnprintf(s_line + n, sizeof(s_line) - n, fmt, ap);
        if (m > 0) n += m;
        if (n > (int)sizeof(s_line) - 2) n = sizeof(s_line) - 2;
        s_line[n++] = '\n';
        append(s_line, n);
        count(lvl[0]);
        s_line[n - 1] = '\r';
        s_line[n++] = '\n';
        // USB väljund eraldi lukuga: kui arvutis keegi porti ei loe, blokeerib
        // Serial.write kuni ajalõpuni – siis ei tohi veebilogi lukk kinni olla,
        // muidu jäid teiste taskide read veebilogist välja. Hõivatud → jäta vahele.
        if (s_serMtx && xSemaphoreTake(s_serMtx, 0) == pdTRUE) {
            memcpy(s_ser, s_line, n);
            unlock();
            Serial.write((const uint8_t *)s_ser, n);
            xSemaphoreGive(s_serMtx);
        } else {
            unlock();
        }
    } else {                                   // enne begin()-i või ISR-ist
        Serial.printf("[%9lu][%s][%s] ", (unsigned long)millis(), lvl, tag);
        char tmp[192];
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
        Serial.print(tmp);
        Serial.print("\r\n");
    }
    va_end(ap);
}

size_t read(uint32_t since, char *dst, size_t max, uint32_t *next) {
    size_t n = 0;
    if (!lock()) { *next = since; return 0; }
    const char *src = s_buf;
    uint32_t size = LOG_BUF_SIZE, total = s_total, base = s_base;
    if (!src) {                                // PSRAM puudub → RTC puhver
        src = s_rtc.buf;
        size = LOG_RTC_SIZE;
        total = s_rtc.total;
        base = 0;
    }
    uint32_t ring = total > size ? total - size : 0;   // vanim alles olev bait
    if (since > total) since = total;          // nt pärast taaskäivitust
    uint32_t pos = since;
    if (pos < base && base >= ring) pos = base;
    if (pos < ring) {                          // üle kirjutatud → alusta terve reaga
        pos = ring;
        while (pos < total && src[pos % size] != '\n') pos++;
        if (pos < total) pos++;
    }
    while (pos < total && n < max) dst[n++] = src[(pos++) % size];
    *next = pos;
    unlock();
    return n;
}

uint32_t position() { return s_buf ? s_total : s_rtc.total; }

void clear() {
    if (!lock()) return;
    s_base = s_total;
    unlock();
}

const char *previous(size_t *len) {
    *len = s_prevLen;
    return s_prev ? s_prev : "";
}

const char *resetReason() { return s_reset; }
const char *resetCode() { return s_resetCode; }
uint32_t warnings() { return s_warn; }
uint32_t errors() { return s_err; }

}  // namespace Log
