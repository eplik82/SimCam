// =============================================================================
//  Püsivad seaded (NVS) – WiFi klient, hotspot, LTE lüliti
// =============================================================================
#pragma once
#include <Arduino.h>

namespace Settings {

struct Data {
    char camName[41];         // kaamera nimi (kuni 40 baiti UTF-8)
    bool staEnabled;
    char staSsid[33];
    char staPass[65];
    bool apEnabled;
    char apSsid[33];
    char apPass[65];
    bool lteEnabled;
    int  rotation;            // kaamerapildi pööre: 0 / 180
    bool mirror;              // peegelda pilt horisontaalselt (sensor hmirror)
    int  framesize;           // kaamera resolutsioon (esp32-camera framesize_t)
    char simPin[9];           // SIM PIN (tühi = PIN-i ei saadeta)
    char apn[64];             // APN, nt operaatori staatilise IP APN
    char webPass[65];         // veebiliidese + RTSP parool
    char authSalt[17];
    bool rtspAuth;            // kas RTSP nõuab parooli
    bool autoUpdate;          // paigalda GitHubi uuendused automaatselt
    bool batEnabled;          // aku on ühendatud (näita olekut veebiliideses)
    float batCal;             // aku pinge kalibreerimistegur (1.0 = ilma parandita)
    bool micEnabled;          // mikrofon sees (vaikimisi väljas – privaatsus)
    int  micGain;             // mikrofoni võimendus dB (0…40)
    int  micCodec;            // 0 = G.711 µ-law 8 kHz, 1 = L16 16 kHz
    int  afMode;              // 0 = ühekordne, 1 = pidev, 2 = käsitsi (AF_MODE_*)
    int  afPos;               // käsitsi fookuse läätse asend 0…1023
    bool rtspAudio;           // lisa helirada RTSP voogu        // juhuslik sool sessiooniküpsise jaoks (hex)
};

void load();                  // loe NVS-ist (puuduvad väärtused = config.h vaikeväärtused)
Data get();                   // lõimekindel koopia
bool save(const Data &d);     // valideeri + salvesta
// Failsafe: kui WiFi klient ja LTE on mõlemad väljas, lülitatakse hotspot sisse
// (muidu poleks seadmele ühtegi teed). Tagastab true, kui hotspot sunniti sisse.
bool applyFailsafe(Data &d);
void resetDefaults();         // tagasi config.h väärtustele

}  // namespace Settings
