// =============================================================================
//  LTE_Manager – teostus
// =============================================================================
#include "modem_lte.h"
#include "config.h"
#include "log.h"

#include <PPP.h>
#include <Preferences.h>
#include "settings.h"

static const char *TAG = "LTE";

#if MODEM_TYPE == MODEM_TYPE_SIM7600
static constexpr ppp_modem_model_t PPP_MODEL = PPP_MODEM_SIM7600;
#else
static constexpr ppp_modem_model_t PPP_MODEL = PPP_MODEM_GENERIC;
#endif

namespace LTE {

// --- Jagatud olek ----------------------------------------------------------
static Status            s_st;
static SemaphoreHandle_t s_mtx = nullptr;
static volatile bool     s_hasIp = false;
static volatile uint32_t s_ipLostAt = 0;
static bool              s_serialOpen = false;
static uint32_t          s_baud = MODEM_BAUD_DEFAULT;
static bool              s_cmuxBroken = false;   // CMUX ebaõnnestus → edaspidi DATA
static HardwareSerial   &AT = Serial1;

#define ST_LOCK()   xSemaphoreTake(s_mtx, portMAX_DELAY)
#define ST_UNLOCK() xSemaphoreGive(s_mtx)

static void setState(State s) {
    ST_LOCK();
    s_st.state = s;
    s_st.connected = (s == State::Connected);
    ST_UNLOCK();
    LOGI(TAG, "Olek -> %s", stateName(s));
}

static void setError(const char *msg) {
    ST_LOCK();
    strlcpy(s_st.lastError, msg, sizeof(s_st.lastError));
    ST_UNLOCK();
    LOGE(TAG, "%s", msg);
}

const char *stateName(State s) {
    switch (s) {
        case State::PowerOn:    return "POWER_ON";
        case State::AtInit:     return "AT_INIT";
        case State::PppStart:   return "PPP_START";
        case State::Attaching:  return "ATTACHING";
        case State::Connecting: return "CONNECTING";
        case State::Connected:  return "CONNECTED";
        case State::Recovering: return "RECOVERING";
        case State::SimError:   return "SIM_ERROR";
    }
    return "?";
}

// =============================================================================
//  Toore AT-suhtluse abifunktsioonid (enne PPP käivitamist, Serial1 kaudu)
// =============================================================================
static void serialOpen(uint32_t baud) {
    if (!s_serialOpen) {
        AT.setRxBufferSize(1024);
        AT.begin(baud, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
        s_serialOpen = true;
    } else {
        AT.updateBaudRate(baud);
    }
    s_baud = baud;
    delay(30);
}

static void serialClose() {
    if (s_serialOpen) {
        AT.end();          // vabastab UART draiveri – PPP/esp_modem installib oma
        s_serialOpen = false;
    }
}

// Kas vastuses on lõplik tulemuskood (OK / ERROR / +CME ERROR)?
static int finalResult(const String &buf) {
    int start = 0;
    while (start < (int)buf.length()) {
        int end = buf.indexOf('\n', start);
        if (end < 0) break;                       // rida pole veel lõpetatud
        String line = buf.substring(start, end);
        line.trim();
        if (line == "OK") return 1;
        if (line == "ERROR" || line.startsWith("+CME ERROR") || line.startsWith("+CMS ERROR")) return -1;
        start = end + 1;
    }
    return 0;
}

// Saada AT-käsk ja oota OK/ERROR. `out` saab kogu vastuse (ilma kajata).
static bool atCmd(const char *cmd, String *out = nullptr, uint32_t timeoutMs = 1000, bool quiet = false) {
    while (AT.available()) AT.read();
    AT.print(cmd);
    AT.print("\r");

    String buf;
    uint32_t t0 = millis();
    int res = 0;
    while (millis() - t0 < timeoutMs) {
        while (AT.available()) buf += (char)AT.read();
        if ((res = finalResult(buf)) != 0) break;
        delay(5);
    }
    buf.trim();
    if (!quiet) LOGI(TAG, ">> %s  <<  %s", cmd, res == 0 ? "(timeout)" : buf.c_str());
    if (out) *out = buf;
    return res == 1;
}

// Proovi modemiga rääkida sihtkiirusel ja vaikekiirusel.
static bool modemResponds() {
    const uint32_t bauds[] = {MODEM_BAUD_TARGET, MODEM_BAUD_DEFAULT};
    for (uint32_t b : bauds) {
        serialOpen(b);
        for (int i = 0; i < 3; i++) {
            if (atCmd("AT", nullptr, 300, true)) {
                LOGI(TAG, "Modem vastab @ %lu baud", (unsigned long)b);
                return true;
            }
        }
        if (MODEM_BAUD_TARGET == MODEM_BAUD_DEFAULT) break;
    }
    return false;
}

// Kui modem on jäänud PPP andmerežiimi (nt ESP taaskäivitus), välju sellest.
static void escapeDataMode() {
    delay(1100);
    AT.print("+++");
    delay(1100);
    atCmd("ATH", nullptr, 1000, true);
}

// T-SIMCAM: GPIO48 "PCIE_PWR" – HIGH-impulss toimib modemi PWRKEY-na.
static void pwrkeyPulse(uint32_t ms) {
    LOGI(TAG, "PWRKEY impulss %lu ms (GPIO%d)", (unsigned long)ms, MODEM_PWRKEY_PIN);
    digitalWrite(MODEM_PWRKEY_PIN, HIGH);
    delay(ms);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);
}

static bool powerOnModem() {
    pinMode(MODEM_PWRKEY_PIN, OUTPUT);
    digitalWrite(MODEM_PWRKEY_PIN, LOW);

    if (MODEM_RESET_PIN >= 0) {
        pinMode(MODEM_RESET_PIN, OUTPUT);
        digitalWrite(MODEM_RESET_PIN, LOW);
        delay(300);
        digitalWrite(MODEM_RESET_PIN, HIGH);
        delay(500);
    }

    // 1) Modem võib juba töötada (nt ainult ESP taaskäivitus) – siis
    //    PWRKEY-d EI vajutata, sest see võiks modemi hoopis välja lülitada.
    if (modemResponds()) return true;
    escapeDataMode();
    if (modemResponds()) return true;

    // 2) Lülita sisse PWRKEY impulsiga (sama järjestus mis LilyGO näidetes)
    pwrkeyPulse(500);
    LOGI(TAG, "Ootan modemi käivitumist (kuni 30 s)...");
    uint32_t t0 = millis();
    while (millis() - t0 < 30000) {
        if (modemResponds()) return true;
        delay(1000);
    }
    return false;
}

// --- Vastuste parsimine ----------------------------------------------------
// Leia "+XXX:" järel olevad komaga eraldatud väljad.
static int fieldsAfter(const String &resp, const char *prefix, String *fields, int maxFields) {
    int p = resp.indexOf(prefix);
    if (p < 0) return 0;
    p += strlen(prefix);
    int eol = resp.indexOf('\n', p);
    String body = resp.substring(p, eol < 0 ? resp.length() : eol);
    body.trim();
    int n = 0;
    bool inQuote = false;
    String cur;
    for (size_t i = 0; i < body.length() && n < maxFields; i++) {
        char c = body[i];
        if (c == '"') { inQuote = !inQuote; continue; }
        if (c == ',' && !inQuote) { cur.trim(); fields[n++] = cur; cur = ""; continue; }
        cur += c;
    }
    if (n < maxFields) { cur.trim(); fields[n++] = cur; }
    return n;
}

static void parseCsq(const String &r) {
    String f[2];
    if (fieldsAfter(r, "+CSQ:", f, 2) < 1) return;
    int csq = f[0].toInt();
    ST_LOCK();
    s_st.csq = csq;
    s_st.rssiDbm = (csq >= 0 && csq <= 31) ? -113 + 2 * csq : 0;
    ST_UNLOCK();
}

// 3GPP TS 27.007 AT+CESQ: rxlev,ber,rscp,ecno,rsrq,rsrp
static void parseCesq(const String &r) {
    String f[6];
    if (fieldsAfter(r, "+CESQ:", f, 6) < 6) return;
    int rsrq = f[4].toInt(), rsrp = f[5].toInt();
    ST_LOCK();
    s_st.rsrqDb  = (rsrq >= 0 && rsrq <= 34) ? -20.0f + rsrq * 0.5f : 0;
    s_st.rsrpDbm = (rsrp >= 0 && rsrp <= 97) ? -141 + rsrp : 0;
    ST_UNLOCK();
}

// +COPS: 0,0,"Telia EE",7
static void parseCops(const String &r) {
    String f[4];
    int n = fieldsAfter(r, "+COPS:", f, 4);
    if (n < 3) return;
    const char *tech = "-";
    if (n >= 4) {
        switch (f[3].toInt()) {
            case 0: case 1: case 3: tech = "GSM"; break;
            case 2: case 4: case 5: case 6: tech = "UMTS"; break;
            case 7: tech = "LTE"; break;
            case 8: tech = "LTE-M"; break;
            case 9: tech = "NB-IoT"; break;
        }
    }
    ST_LOCK();
    strlcpy(s_st.op, f[2].c_str(), sizeof(s_st.op));
    strlcpy(s_st.tech, tech, sizeof(s_st.tech));
    ST_UNLOCK();
}

// +CEREG: <n>,<stat>[,...]
static void parseCereg(const String &r) {
    String f[2];
    if (fieldsAfter(r, "+CEREG:", f, 2) < 2) return;
    ST_LOCK();
    s_st.regStat = f[1].toInt();
    ST_UNLOCK();
}

// =============================================================================
//  SIM PIN – kaitse SIM-i lukustumise (PUK) vastu
// =============================================================================
// Kui PIN lükati tagasi, salvestame selle NVS-i. Sama PIN-i ei proovita enam
// kunagi (ka mitte pärast taaskäivitust). Uus PIN config.h-s → proovitakse.
static bool pinKnownBad(const char *pin) {
    Preferences p;
    p.begin("simcam", true);
    String bad = p.getString("badpin", "");
    p.end();
    return bad == pin;
}

static void markPin(const char *pin, bool good) {
    Preferences p;
    p.begin("simcam", false);
    if (good) p.remove("badpin");
    else p.putString("badpin", pin);
    p.end();
}

// Tagastab: 1 = SIM valmis, 0 = proovi hiljem uuesti, -1 = fataalne (PIN/PUK)
static int unlockSim() {
    String r;
    uint32_t t0 = millis();
    while (millis() - t0 < 30000) {
        atCmd("AT+CPIN?", &r, 5000);
        if (r.indexOf("READY") >= 0) return 1;

        if (r.indexOf("SIM PUK") >= 0) {
            setError("SIM on PUK lukus! Ava SIM telefonis PUK koodiga.");
            return -1;
        }
        if (r.indexOf("SIM PIN") >= 0) {
            const Settings::Data cfg = Settings::get();
            if (!cfg.simPin[0]) {
                setError("SIM küsib PIN-i, aga PIN on seadistamata (⚙ → Mobiilivõrk)");
                return -1;
            }
            if (pinKnownBad(cfg.simPin)) {
                setError("See PIN lükati varem tagasi – ei proovi uuesti (PUK kaitse)");
                return -1;
            }
            LOGI(TAG, "SIM küsib PIN-i, saadan AT+CPIN=\"****\"");
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CPIN=\"%s\"", cfg.simPin);
            if (!atCmd(cmd, &r, 15000, true)) {
                markPin(cfg.simPin, false);
                setError("SIM PIN vale! Paranda PIN seadetes (⚙ → Mobiilivõrk).");
                return -1;
            }
            markPin(cfg.simPin, true);
            delay(3000);               // SIM initsialiseerub pärast PIN-i
            continue;
        }
        if (r.indexOf("NOT INSERTED") >= 0 || r.indexOf("not inserted") >= 0) {
            setError("SIM kaarti ei leitud");
            return 0;
        }
        delay(1000);                   // "SIM busy" vms – oota
    }
    setError("SIM ei saanud valmis (timeout)");
    return 0;
}

// =============================================================================
//  AT initsialiseerimine: kiirus, info, PIN, APN
// =============================================================================
static int atInit() {
    String r;
    atCmd("ATE0");                      // kaja välja
    atCmd("AT+CMEE=2");                 // tekstilised veateated

    // --- UART kiiruse tõstmine (PPP läbilaskevõime sõltub sellest) ---------
    if (s_baud != MODEM_BAUD_TARGET) {
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "AT+IPR=%lu", (unsigned long)MODEM_BAUD_TARGET);
        if (atCmd(cmd, &r, 1000)) {
            serialOpen(MODEM_BAUD_TARGET);
            delay(100);
            if (!atCmd("AT", nullptr, 500) && !atCmd("AT", nullptr, 500)) {
                LOGW(TAG, "Modem ei vasta %lu baudil, jään %d peale",
                     (unsigned long)MODEM_BAUD_TARGET, MODEM_BAUD_DEFAULT);
                serialOpen(MODEM_BAUD_DEFAULT);
                snprintf(cmd, sizeof(cmd), "AT+IPR=%d", MODEM_BAUD_DEFAULT);
                atCmd(cmd);
            }
        } else {
            LOGW(TAG, "Modem ei toeta AT+IPR=%lu", (unsigned long)MODEM_BAUD_TARGET);
        }
    }

    // --- Modemi info -------------------------------------------------------
    if (atCmd("AT+CGMM", &r)) {
        r.replace("OK", "");
        r.trim();
        ST_LOCK(); strlcpy(s_st.model, r.c_str(), sizeof(s_st.model)); ST_UNLOCK();
    }
    if (atCmd("AT+CGSN", &r)) {
        String imei;
        for (char c : r) if (isDigit(c)) imei += c;
        ST_LOCK(); strlcpy(s_st.imei, imei.c_str(), sizeof(s_st.imei)); ST_UNLOCK();
    }

    // --- SIM PIN ------------------------------------------------------------
    int sim = unlockSim();
    if (sim <= 0) return sim;

    // --- APN (PDP kontekst 1) ----------------------------------------------
    // LTE-s kasutatakse konteksti 1 APN-i juba võrku registreerumisel
    // (default bearer). Kui see on vale, lülitame raadio korraks välja,
    // et uus APN kehtiks kohe ja saaksime staatilise IP.
    const Settings::Data cfg = Settings::get();
    if (!cfg.apn[0]) {
        setError("APN on seadistamata (⚙ → Mobiilivõrk)");
        return -1;
    }
    char want[80];
    snprintf(want, sizeof(want), "\"%s\"", cfg.apn);
    atCmd("AT+CGDCONT?", &r, 2000);
    if (r.indexOf(want) < 0) {
        LOGI(TAG, "Seadistan APN-i: %s", cfg.apn);
        atCmd("AT+CFUN=4", nullptr, 10000);
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", cfg.apn);
        atCmd(cmd, nullptr, 2000);
        atCmd("AT+CFUN=1", nullptr, 10000);
        delay(2000);
        if (unlockSim() <= 0) return 0;
    } else {
        atCmd("AT+CFUN=1", nullptr, 10000);
    }

    atCmd("AT+COPS=3,0");               // operaatori nimi pikas tekstivormingus
    atCmd("AT+CEREG=0");                // lihtne CEREG vastuse formaat
    if (atCmd("AT+CSQ", &r)) parseCsq(r);

    ST_LOCK(); s_st.baud = s_baud; ST_UNLOCK();
    return 1;
}

// =============================================================================
//  PPP sündmused
// =============================================================================
static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_PPP_START:     LOGI(TAG, "PPP käivitatud"); break;
        case ARDUINO_EVENT_PPP_CONNECTED: LOGI(TAG, "PPP ühendatud"); break;
        case ARDUINO_EVENT_PPP_GOT_IP:
            s_hasIp = true;
            LOGI(TAG, "PPP sai IP: %s", PPP.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_PPP_LOST_IP:
        case ARDUINO_EVENT_PPP_DISCONNECTED:
        case ARDUINO_EVENT_PPP_STOP:
            if (s_hasIp) s_ipLostAt = millis();
            s_hasIp = false;
            LOGW(TAG, "PPP ühendus katkes (event %d)", (int)event);
            break;
        default: break;
    }
}

// Signaali ja registreeringu päring PPP ajal (vajab CMUX-i)
static void pollModem() {
    if (PPP.mode() == ESP_MODEM_MODE_DATA) return;   // AT kanal pole saadaval
    String r;
    if (PPP.cmd("AT+CSQ", r, 1000))   parseCsq(r);
    if (PPP.cmd("AT+CESQ", r, 1000))  parseCesq(r);
    if (PPP.cmd("AT+COPS?", r, 3000)) parseCops(r);
    if (PPP.cmd("AT+CEREG?", r, 1000)) parseCereg(r);
}

// --- SIM PIN-i muutmise päring (täidetakse LTE taskis, et AT kanal oleks jagamata)
static SemaphoreHandle_t s_pinDone = nullptr;
static volatile bool     s_pinReq = false;
static char              s_pinOld[9], s_pinNew[9];
static bool              s_pinOk = false;
static String            s_pinErr;

static void servicePinChange() {
    if (!s_pinReq) return;
    s_pinReq = false;
    s_pinOk = false;
    if (PPP.mode() != ESP_MODEM_MODE_CMUX) {
        s_pinErr = "Modem on andmerežiimis (CMUX puudub) – PIN-i ei saa praegu muuta";
    } else {
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "AT+CPWD=\"SC\",\"%s\",\"%s\"", s_pinOld, s_pinNew);
        String r;
        s_pinOk = PPP.cmd(cmd, r, 10000);
        if (s_pinOk) {
            Settings::Data d = Settings::get();
            strlcpy(d.simPin, s_pinNew, sizeof(d.simPin));
            Settings::save(d);
            markPin(s_pinNew, true);
            LOGI(TAG, "SIM-kaardi PIN muudetud");
        } else {
            s_pinErr = r.indexOf("incorrect") >= 0 || r.indexOf("16") >= 0
                           ? "Vana PIN on vale (NB! 3 valet katset lukustab SIM-i PUK-iga)"
                           : "Modem lükkas PIN-i muutmise tagasi: " + r;
            LOGW(TAG, "SIM PIN-i muutmine ebaõnnestus: %s", r.c_str());
        }
    }
    memset(s_pinOld, 0, sizeof(s_pinOld));
    memset(s_pinNew, 0, sizeof(s_pinNew));
    xSemaphoreGive(s_pinDone);
}

bool changeSimPin(const char *oldPin, const char *newPin, String &err) {
    if (!s_mtx) { err = "LTE modem on seadetes välja lülitatud"; return false; }
    if (!status().connected) { err = "Modem peab olema võrku ühendatud (olek CONNECTED)"; return false; }
    if (!s_pinDone) s_pinDone = xSemaphoreCreateBinary();
    xSemaphoreTake(s_pinDone, 0);
    strlcpy(s_pinOld, oldPin, sizeof(s_pinOld));
    strlcpy(s_pinNew, newPin, sizeof(s_pinNew));
    s_pinReq = true;
    if (xSemaphoreTake(s_pinDone, pdMS_TO_TICKS(15000)) != pdTRUE) {
        s_pinReq = false;
        err = "Modem ei vastanud";
        return false;
    }
    if (!s_pinOk) err = s_pinErr;
    return s_pinOk;
}

static bool registered() {
    ST_LOCK();
    int st = s_st.regStat;
    ST_UNLOCK();
    return st == 1 || st == 5 || st == -1;   // -1 = pole teada → ära karista
}

// =============================================================================
//  Halduri task (olekumasin)
// =============================================================================
static void lteTask(void *) {
    State state = State::PowerOn;
    uint32_t backoff = 5000;
    uint32_t lastConnectedMs = millis();
    uint32_t failedRecoveries = 0;
    uint32_t lastPoll = 0, regLostAt = 0;

    for (;;) {
        setState(state);
        switch (state) {

        case State::PowerOn:
            if (powerOnModem()) state = State::AtInit;
            else {
                // Levinuim põhjus: USB-ainult mPCIe kaart (nt MikroTik R11e-LTE),
                // mis ei kasuta T-SIMCAM-i UART liine. Vt README "Modemi ühilduvus".
                setError("Modem ei vasta UART-il (GPIO45/46). USB-kaardid nagu R11e-LTE ei sobi!");
                state = State::Recovering;
            }
            break;

        case State::AtInit: {
            int r = atInit();
            state = r > 0 ? State::PppStart : (r < 0 ? State::SimError : State::Recovering);
            break;
        }

        case State::PppStart:
            serialClose();
            PPP.setApn(Settings::get().apn);
            PPP.setPins(MODEM_TX_PIN, MODEM_RX_PIN);   // RTS/CTS pole ühendatud
            if (PPP.begin(PPP_MODEL, MODEM_UART_NUM, s_baud)) {
                state = State::Attaching;
            } else {
                setError("PPP.begin ebaõnnestus");
                state = State::Recovering;
            }
            break;

        case State::Attaching: {
            uint32_t t0 = millis();
            bool att = false;
            while (millis() - t0 < LTE_ATTACH_TIMEOUT_MS) {
                if ((att = PPP.attached())) break;
                String r;
                if (PPP.cmd("AT+CSQ", r, 1000)) parseCsq(r);
                LOGI(TAG, "Ootan võrku (CSQ %d)...", status().csq);
                delay(2000);
            }
            if (!att) { setError("Võrku registreerumine ebaõnnestus (timeout)"); state = State::Recovering; break; }
            String r;
            if (PPP.cmd("AT+COPS?", r, 3000)) parseCops(r);
            state = State::Connecting;
            break;
        }

        case State::Connecting: {
            s_hasIp = false;
            bool ok = false;
#if MODEM_USE_CMUX
            if (!s_cmuxBroken) {
                ok = PPP.mode(ESP_MODEM_MODE_CMUX);
                if (!ok) { LOGW(TAG, "CMUX ei toimi – kasutan DATA režiimi"); s_cmuxBroken = true; }
            }
#endif
            if (!ok) ok = PPP.mode(ESP_MODEM_MODE_DATA);
            if (!ok) { setError("PPP andmerežiimi ei saanud"); state = State::Recovering; break; }

            uint32_t t0 = millis();
            while (!s_hasIp && millis() - t0 < LTE_IP_TIMEOUT_MS) delay(200);
            if (!s_hasIp) { setError("PPP ei saanud IP aadressi"); state = State::Recovering; break; }

            String ip = PPP.localIP().toString();
            ST_LOCK();
            strlcpy(s_st.ip, ip.c_str(), sizeof(s_st.ip));
            const char *expIp = Settings::get().expectedIp;
            s_st.ipMatchesExpected = !expIp[0] || ip == expIp;
            s_st.cmux = (PPP.mode() == ESP_MODEM_MODE_CMUX);
            s_st.connectedSinceMs = millis();
            s_st.lastError[0] = 0;
            ST_UNLOCK();
            // Internetist tulnud ühenduste vastused peavad minema LTE kaudu,
            // ka siis, kui WiFi on samal ajal ühendatud.
            PPP.setDefault();
            if (expIp[0] && ip != expIp)
                LOGW(TAG, "IP %s ei ole oodatud %s – kontrolli APN-i/staatilise IP teenust",
                     ip.c_str(), expIp);
            LOGI(TAG, "*** ONLINE: rtsp://%s:%d%s  http://%s/ ***",
                 ip.c_str(), RTSP_PORT, RTSP_PATH, ip.c_str());
            backoff = 5000;
            failedRecoveries = 0;
            regLostAt = 0;
            lastPoll = 0;
            state = State::Connected;
            break;
        }

        case State::Connected:
            // Püsime selles olekus, kuni ühendus katkeb
            while (true) {
                uint32_t now = millis();
                lastConnectedMs = now;
                if (now - lastPoll >= LTE_MONITOR_PERIOD_MS) {
                    lastPoll = now;
                    pollModem();
                    if (!registered()) { if (!regLostAt) regLostAt = now; }
                    else regLostAt = 0;
                }
                servicePinChange();
                if (!s_hasIp && now - s_ipLostAt > LTE_LINK_LOST_GRACE_MS) {
                    setError("IP kadunud – taastan ühenduse");
                    break;
                }
                if (regLostAt && now - regLostAt > LTE_LINK_LOST_GRACE_MS) {
                    setError("Võrgu registreering kadunud – taastan ühenduse");
                    break;
                }
                delay(500);
            }
            state = State::Recovering;
            break;

        case State::Recovering: {
            ST_LOCK();
            s_st.reconnects++;
            strlcpy(s_st.ip, "-", sizeof(s_st.ip));
            s_st.ipMatchesExpected = false;
            ST_UNLOCK();
            PPP.end();
            s_hasIp = false;
            failedRecoveries++;
            LOGW(TAG, "Taastamine #%lu, ootan %lu ms", (unsigned long)failedRecoveries, (unsigned long)backoff);
            delay(backoff);
            backoff = min<uint32_t>(backoff * 2, 60000);

            // Korduvate ebaõnnestumiste korral modemi täielik taaskäivitus
            if (failedRecoveries >= 3 && powerOnModem()) {
                LOGW(TAG, "Modemi taaskäivitus (AT+CFUN=1,1)");
                atCmd("AT+CFUN=1,1", nullptr, 5000);
                serialClose();
                delay(15000);
            }
            state = State::PowerOn;
            break;
        }

        case State::SimError:
            // PIN/PUK viga – EI proovita automaatselt uuesti ega taaskäivitata,
            // et SIM ei lukustuks. Kontrollime vaid minutiti, kas SIM on vahetatud.
            PPP.end();
            delay(60000);
            if (powerOnModem()) {
                String r;
                atCmd("AT+CPIN?", &r, 5000);
                if (r.indexOf("READY") >= 0) state = State::AtInit;
            }
            lastConnectedMs = millis();
            break;
        }

        // Viimane kaitse: kui ühendust pole kaua olnud, taaskäivita kogu ESP
        if (state != State::Connected && state != State::SimError &&
            millis() - lastConnectedMs > LTE_REBOOT_AFTER_MS) {
            LOGE(TAG, "Ühendust pole %lu min – ESP32 taaskäivitus", LTE_REBOOT_AFTER_MS / 60000);
            delay(500);
            ESP.restart();
        }
    }
}

// =============================================================================
//  Avalik API
// =============================================================================
bool enabled() { return s_mtx != nullptr; }

void begin() {
    s_mtx = xSemaphoreCreateMutex();
    Network.onEvent(onNetEvent);
    // Core 0 – samas kus lwIP/PPP; kaamera ja serverid core 1 peal.
    xTaskCreatePinnedToCore(lteTask, "lte_mgr", 8192, nullptr, 4, nullptr, 0);
}

Status status() {
    Status c;
    if (!s_mtx) return c;
    ST_LOCK();
    c = s_st;
    ST_UNLOCK();
    return c;
}

bool connected() { return s_hasIp && status().connected; }

}  // namespace LTE
