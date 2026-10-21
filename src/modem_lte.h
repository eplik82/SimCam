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
    bool     cmux = false;
    char     ip[16] = "-";
    char     op[32] = "-";           // operaator (nt "Telia EE")
    char     tech[12] = "-";         // LTE / UMTS / GSM
    char     model[32] = "-";
    char     imei[20] = "-";
    char     fwRev[40] = "-";         // modemi püsivara (AT+CGMR)
    char     iccid[24] = "-";         // SIM-kaardi ICCID
    int      tempC = -999;            // modemi temperatuur (AT+CPMUTEMP), -999 = teadmata
    float    vbat = 0;                // modemi toitepinge V (AT+CBC), 0 = teadmata
    char     band[20] = "-";          // sagedusala, nt "EUTRAN-BAND3" (AT+CPSI)
    int      earfcn = 0;
    int      sinrDb = -999;           // RSSNR/SINR dB, -999 = teadmata
    char     cellId[16] = "-";        // teenindava lahtri ID
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

// Modemi GNSS (SIM7600: AT+CGPS / AT+CGPSINFO). Loetakse LTE taskis CMUX AT-kanali kaudu.
struct Gps {
    bool     supported = false;      // modem vastas AT+CGPS? päringule
    bool     on = false;             // GNSS mootor töötab
    bool     fix = false;            // viimane päring andis asukoha
    double   lat = 0, lon = 0;       // kraadid (WGS84), viimane teadaolev asukoht
    float    altM = 0;               // kõrgus merepinnast (m)
    float    speedKmh = 0;
    float    course = 0;             // liikumissuund (°)
    int      sats = -1;              // kasutatud satelliite (-1 = teadmata)
    char     utc[24] = "";           // satelliidiaeg "2026-10-21 12:34:56"
    uint32_t fixAtMs = 0;            // viimase asukoha aeg (millis), 0 = pole kunagi olnud
    bool     active = false;         // asukoha uuendus käib (GNSS sees)
    uint32_t nextAtMs = 0;           // järgmise uuenduse aeg (millis), kui !active
    char     note[80] = "";          // miks GPS-i ei loeta (nt CMUX puudub)
};

void begin();                 // käivitab halduri taski (mitteblokeeriv)
Gps gps();                    // lõimekindel koopia GPS olekust
bool enabled();               // kas begin() on välja kutsutud
Status status();              // lõimekindel koopia hetkeolekust
bool connected();
const char *stateName(State s);

// Muuda SIM-kaardi PIN-i (AT+CPWD="SC"). Blokeerib kuni ~15 s.
// Vajab töötavat modemit (olek CONNECTED + CMUX). Õnnestumisel salvestatakse
// uus PIN ka seadetesse. `err` saab tekstilise veateate.
bool changeSimPin(const char *oldPin, const char *newPin, String &err);

}  // namespace LTE
