// =============================================================================
//  Aku: pinge, täituvus ja olek
//
//  T-SIMCAM V1.3: TP4056 laadija (laadimisvool ~600 mA, PROG = 2 kΩ) ja aku
//  pinge jagur 100 k / 100 k → GPIO3 (BAT_ADC). Laadija CHRG/STDBY viigud on
//  ühendatud ainult plaadi LED-iga ja voolumõõtmist plaadil pole – seetõttu
//  tuletatakse olek (laeb / tühjeneb / täis) pinge muutumise kiirusest.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace Battery {

enum class State : uint8_t {
    Measuring,     // vähe andmeid (esimesed minutid)
    Absent,        // pinget pole → aku puudub
    Charging,      // pinge tõuseb
    Full,          // ≥ 4,15 V ja ei lange → täis / laadijal
    Discharging,   // pinge langeb
    Low,           // tühjeneb ja ≤ 15 %
    Stable,        // muutus alla mõõtetäpsuse
};

struct Status {
    bool     enabled;      // seadetes "aku ühendatud"
    State    state;
    float    voltage;      // V (filtreeritud, kalibreeritud)
    float    raw;          // V ilma kalibreerimiseta
    int      percent;      // 0…100 (Li-ion tühjenemiskõvera järgi)
    float    slope;        // mV/min (10 min regressioon), + = laeb
    int      minutesLeft;  // hinnanguline tööaeg (tühjenemisel), -1 = teadmata
    int      histMinutes;  // mitu minutit mõõtmisi on kogunenud (max 24 h)
    bool     usbHost;      // USB on arvutiga ühendatud (laadija saab toidet)
};

void begin();
Status status();
const char *stateName(State s);   // ASCII kood JSON-i jaoks

// Ajalugu: üks punkt iga BAT_HIST_SEC sekundi järel (mV), vanim → uusim
#define BAT_HIST_SEC  30
#define BAT_HIST_LEN  2880        // 24 h
size_t history(uint16_t *dst, size_t max);

// Kalibreerimine multimeetriga mõõdetud pinge järgi; tagastab uue teguri (0 = viga)
float calibrate(float measuredV);

}  // namespace Battery
