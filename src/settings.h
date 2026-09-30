// =============================================================================
//  Püsivad seaded (NVS) – WiFi klient, hotspot, LTE lüliti
// =============================================================================
#pragma once
#include <Arduino.h>

namespace Settings {

struct Data {
    bool staEnabled;
    char staSsid[33];
    char staPass[65];
    bool apEnabled;
    char apSsid[33];
    char apPass[65];
    bool lteEnabled;
    int  rotation;            // kaamerapildi pööre: 0 / 90 / 180 / 270 (päripäeva)
    char simPin[9];           // SIM PIN (tühi = PIN-i ei saadeta)
    char apn[64];             // APN, nt operaatori staatilise IP APN
    char webPass[65];         // veebiliidese + RTSP parool
    char authSalt[17];
    bool rtspAuth;            // kas RTSP nõuab parooli
    bool autoUpdate;          // paigalda GitHubi uuendused automaatselt        // juhuslik sool sessiooniküpsise jaoks (hex)
};

void load();                  // loe NVS-ist (puuduvad väärtused = config.h vaikeväärtused)
Data get();                   // lõimekindel koopia
bool save(const Data &d);     // valideeri + salvesta
// Failsafe: kui WiFi klient ja LTE on mõlemad väljas, lülitatakse hotspot sisse
// (muidu poleks seadmele ühtegi teed). Tagastab true, kui hotspot sunniti sisse.
bool applyFailsafe(Data &d);
void resetDefaults();         // tagasi config.h väärtustele

}  // namespace Settings
