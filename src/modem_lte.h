// =============================================================================
//  LTE_Manager – mPCIe UART-modemi (nt SIM7600) haldus, PPP ühendus ja taastamine
// =============================================================================
//  Töövoog (eraldi FreeRTOS taskis, olekumasin):
//
//   POWER_ON ─► AT_INIT ─► PPP_START ─► ATTACHING ─► CONNECTING ─► CONNECTED
//      ▲          │ (PIN, APN, baud)                                  │
//      │          ▼                                                   │ IP kadunud /
//      └──── RECOVERING ◄─────────────────────────────────────────────┘ võrk kadunud
//
//  * PIN saadetakse ainult siis, kui modem küsib (AT+CPIN? → "SIM PIN") ja
//    ainult ÜKS kord püsivara töö jooksul – vale PIN ei lukusta SIM-i (PUK).
//  * PPP töötab CMUX režiimis, et AT-päringud (CSQ/CESQ/COPS) toimiksid ka
//    andmeühenduse ajal. Kui modem CMUX-i ei toeta → tavaline DATA režiim.
// =============================================================================
#pragma once

#include <Arduino.h>

namespace LTE {

enum class State : uint8_t {
    PowerOn,
    AtInit,
    PppStart,
    Attaching,
    Connecting,
    Connected,
    Recovering,
    SimError,
};

struct Status {
    State    state = State::PowerOn;
    bool     connected = false;
    bool     ipMatchesExpected = false;
    bool     cmux = false;
    char     ip[16] = "-";
    char     op[32] = "-";           // operaator (nt "Telia EE")
    char     tech[12] = "-";         // LTE / UMTS / GSM
    char     model[32] = "-";
    char     imei[20] = "-";
    char     lastError[96] = "";
    int      csq = 99;               // 0..31, 99 = teadmata
    int      rssiDbm = 0;            // 0 = teadmata
    int      rsrpDbm = 0;            // 0 = teadmata (LTE, AT+CESQ)
    float    rsrqDb = 0;             // 0 = teadmata
    int      regStat = -1;           // +CEREG stat (1=kodu, 5=rändlus)
    uint32_t baud = 0;
    uint32_t connectedSinceMs = 0;
    uint32_t reconnects = 0;
};

void begin();                 // käivitab halduri taski (mitteblokeeriv)
bool enabled();               // kas begin() on välja kutsutud
Status status();              // lõimekindel koopia hetkeolekust
bool connected();
const char *stateName(State s);

}  // namespace LTE
