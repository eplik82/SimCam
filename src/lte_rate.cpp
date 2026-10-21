// =============================================================================
//  LteRate – teostus
// =============================================================================
#include "lte_rate.h"
#include "config.h"
#include "log.h"
#include "lwip/sockets.h"

static const char *TAG = "RATE";

namespace LteRate {

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_ip = 0;            // PPP IP (võrgu baidijärjestus), 0 = LTE maas

static float    s_rate = LTE_RATE_START_KBPS; // eelarve kB/s
static float    s_tokens = 0;                 // baiti
static uint32_t s_refillAt = 0;
static uint32_t s_lastFrameAt = 0, s_limitedAt = 0, s_holdUntil = 0, s_lastCut = 0, s_lastProbe = 0;
static uint32_t s_rtt = 0, s_base = 0, s_cuts = 0;
static uint8_t  s_badRun = 0, s_warnRun = 0;    // järjestikused halvad mõõtmised
static uint32_t s_winAt = 0, s_winBytes = 0, s_winFrames = 0;
static float    s_sentKBps = 0, s_fps = 0;

static float s_cap = LTE_RATE_CAP_WEAK_KBPS;  // eelarve lagi (levi järgi)
static float s_rsrp = 0;                      // RSRP libisev keskmine, 0 = teadmata

static float capFor(float rsrp) {
    if (rsrp == 0) return LTE_RATE_CAP_WEAK_KBPS;
    if (rsrp >= LTE_RATE_RSRP_GOOD) return LTE_RATE_CAP_GOOD_KBPS;
    if (rsrp >= LTE_RATE_RSRP_MID) return LTE_RATE_CAP_MID_KBPS;
    return LTE_RATE_CAP_WEAK_KBPS;
}

static float clampRate(float r) {
    return r < LTE_RATE_MIN_KBPS ? LTE_RATE_MIN_KBPS : (r > s_cap ? s_cap : r);
}

static float capBytes() { return s_rate * 1024.0f * LTE_RATE_BURST_S; }

// Kutsuda lukus
static void refill(uint32_t now) {
    float dt = (now - s_refillAt) / 1000.0f;
    s_refillAt = now;
    s_tokens += s_rate * 1024.0f * dt;
    if (s_tokens > capBytes()) s_tokens = capBytes();
}

// Kutsuda lukus
static void account(size_t bytes, uint32_t now, bool frame) {
    s_winBytes += bytes;
    if (frame) { s_winFrames++; s_lastFrameAt = now; }
    if (now - s_winAt >= 2000) {
        float dt = (now - s_winAt) / 1000.0f;
        s_sentKBps = s_winBytes / 1024.0f / dt;
        s_fps = s_winFrames / dt;
        s_winAt = now;
        s_winBytes = s_winFrames = 0;
    }
}

static bool activeLocked(uint32_t now) { return s_lastFrameAt && now - s_lastFrameAt < 3000; }

// Eelarve kohe alla ja paus, et modemi järjekord tühjeneks
static void cut(float factor, const char *why, uint32_t val) {
    const uint32_t now = millis();
    float before, after;
    portENTER_CRITICAL(&s_mux);
    if (s_lastCut && now - s_lastCut < LTE_RATE_HOLD_MS / 2) { portEXIT_CRITICAL(&s_mux); return; }
    s_lastCut = now;
    before = s_rate;
    s_rate = clampRate(s_rate * factor);
    after = s_rate;
    if (s_tokens > 0) s_tokens = 0;
    s_holdUntil = now + LTE_RATE_HOLD_MS;
    s_cuts++;
    portEXIT_CRITICAL(&s_mux);
    LOGW(TAG, "LTE eelarve %.0f → %.0f kB/s (%s %lu ms)", before, after, why, (unsigned long)val);
}

void reset(uint32_t localIp, int rsrpDbm) {
    float start = LTE_RATE_START_KBPS;
    if (rsrpDbm) start = rsrpDbm >= -95 ? 45 : (rsrpDbm >= -105 ? 30 : 20);
    portENTER_CRITICAL(&s_mux);
    s_ip = localIp;
    s_rsrp = rsrpDbm;
    s_cap = capFor(s_rsrp);
    s_rate = clampRate(start);
    s_refillAt = millis();
    s_tokens = capBytes();
    s_base = 0;
    s_holdUntil = s_lastCut = 0;
    portEXIT_CRITICAL(&s_mux);
    if (localIp) LOGI(TAG, "LTE vaatajate algeelarve %.0f kB/s (RSRP %d dBm)", start, rsrpDbm);
}

void setSignal(int rsrpDbm) {
    if (!rsrpDbm) return;
    float before, after;
    portENTER_CRITICAL(&s_mux);
    s_rsrp = s_rsrp == 0 ? rsrpDbm : s_rsrp * 0.7f + rsrpDbm * 0.3f;   // ~30 s keskmine
    before = s_cap;
    s_cap = capFor(s_rsrp);
    after = s_cap;
    if (s_rate > s_cap) s_rate = s_cap;
    const float avg = s_rsrp;
    portEXIT_CRITICAL(&s_mux);
    if (before != after) LOGI(TAG, "LTE eelarve lagi %.0f → %.0f kB/s (RSRP keskmine %.0f dBm)", before, after, avg);
}

bool isLteSocket(int fd) {
    if (!s_ip || fd < 0) return false;
    struct sockaddr_storage ss = {};
    socklen_t l = sizeof(ss);
    if (getsockname(fd, (struct sockaddr *)&ss, &l) != 0) return false;
    uint32_t ip = 0;
    if (ss.ss_family == AF_INET) ip = ((struct sockaddr_in *)&ss)->sin_addr.s_addr;
    else if (ss.ss_family == AF_INET6) memcpy(&ip, &((struct sockaddr_in6 *)&ss)->sin6_addr.un.u32_addr[3], 4);
    return ip == s_ip;
}

bool allow(size_t bytes) {
    const uint32_t now = millis();
    bool ok;
    portENTER_CRITICAL(&s_mux);
#if LTE_RATE_ENABLED
    refill(now);
    // suurem kaader kui purske piir → luba täis "rahakoti" pealt (läheb võlga)
    float need = (float)bytes < capBytes() ? (float)bytes : capBytes();
    ok = s_tokens >= need;
#else
    ok = true;
#endif
    if (ok) { s_tokens -= bytes; account(bytes, now, true); }
    else s_limitedAt = now;
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

void consume(size_t bytes) {
    const uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    refill(now);
    s_tokens -= bytes;
    account(bytes, now, false);
    portEXIT_CRITICAL(&s_mux);
}

void sent(size_t bytes, uint32_t ms) {
    (void)bytes;
    if (ms > LTE_RATE_SLOW_SEND_MS) cut(0.7f, "kaadri saatmine venis", ms);
}

uint32_t paceMs(size_t bytes) {
    portENTER_CRITICAL(&s_mux);
    const float rate = s_rate;
    portEXIT_CRITICAL(&s_mux);
    return (uint32_t)(bytes * 1000.0f / (rate * 1024.0f * LTE_RATE_PACE_X));
}

bool wantProbe() {
    const uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    // ka veidi pärast viimast kaadrit: kui kanal jooksis kinni, kaadrid seiskuvad –
    // mõõtmised peavad jätkuma, et kinnijäämine kiiresti tuvastataks
    bool w = s_lastFrameAt && now - s_lastFrameAt < 15000 && now - s_lastProbe >= LTE_RATE_PROBE_MS;
    if (w) s_lastProbe = now;
    portEXIT_CRITICAL(&s_mux);
    return w;
}

void atSample(uint32_t rttMs, bool ok) {
    const uint32_t now = millis();
    int verdict = 0;                     // -2 aegus, -1 halb, 0 hoiatus/ootel, 1 tõsta
    uint32_t base;
    portENTER_CRITICAL(&s_mux);
    s_rtt = ok ? rttMs : 0;
    const bool act = activeLocked(now);
    if (ok && (!s_base || rttMs < s_base)) s_base = rttMs;
    else if (ok && !act) s_base += ((int32_t)rttMs - (int32_t)s_base) / 8;   // tühikäigul aeglaselt üles
    base = s_base;
    // Üksik hüpe = päring sattus kaadri saatmise keskele (ootab UART järjekorras) –
    // järjekorra kasvu näitavad alles kaks järjestikust halba mõõtmist.
    const bool bad = ok && rttMs > base + LTE_RATE_RTT_BAD_MS;
    const bool warn = ok && rttMs > base + LTE_RATE_RTT_WARN_MS;
    s_badRun = bad ? s_badRun + 1 : 0;
    s_warnRun = warn ? s_warnRun + 1 : 0;
    if (act) {
        if (!ok) verdict = -2;
        else if (s_badRun >= 2) { verdict = -1; s_badRun = 0; }
        else if (s_warnRun >= 2) s_rate = clampRate(s_rate - LTE_RATE_STEP_KBPS);  // järjekord kasvab
        else if (!warn && (int32_t)(now - s_holdUntil) >= 0 &&
                 s_limitedAt && now - s_limitedAt < 3000) {
            s_rate = clampRate(s_rate + LTE_RATE_STEP_KBPS);
            verdict = 1;
        }
    }
    const float rate = s_rate;
    portEXIT_CRITICAL(&s_mux);
#if LTE_RATE_DEBUG
    if (act) LOGI(TAG, "AT %s %lu ms (tühikäik %lu) eelarve %.0f kB/s", ok ? "ok" : "AEGUS",
                  (unsigned long)rttMs, (unsigned long)base, rate);
#else
    (void)rate;
#endif
    if (verdict == -2) cut(0.5f, "modem ei vastanud", rttMs);
    else if (verdict == -1) cut(0.7f, "modemi vastamisaeg", rttMs);
}

Status status() {
    Status st;
    const uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    st.active = activeLocked(now);
    st.budgetKBps = s_rate;
    st.capKBps = s_cap;
    st.rsrpDbm = (int)s_rsrp;
    st.sentKBps = st.active ? s_sentKBps : 0;
    st.fps = st.active ? s_fps : 0;
    st.rttMs = s_rtt;
    st.baseRttMs = s_base;
    st.cuts = s_cuts;
    portEXIT_CRITICAL(&s_mux);
    return st;
}

}  // namespace LteRate
