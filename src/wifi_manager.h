// =============================================================================
//  WiFi haldur – klient (STA) + hotspot (AP) korraga, mDNS simcam.local
// =============================================================================
//  * Klient ühendub seadetes määratud võrku ja taasühendub
//    automaatselt.
//  * Hotspot "SimCam" lubab telefoniga otse ühenduda: http://4.3.2.1 (avaneb ise –
//    captive portal) ja rtsp://4.3.2.1:554/live
//  * RTSP ja veebiserver kuulavad kõigil liidestel (WiFi, AP, LTE).
// =============================================================================
#pragma once
#include <Arduino.h>

namespace WifiMgr {

struct Status {
    bool    staEnabled = false;
    bool    staConnected = false;
    char    staSsid[33] = "";
    char    staIp[16] = "-";
    int     staRssi = 0;
    int     channel = 0;
    bool    apEnabled = false;
    char    apSsid[33] = "";
    char    apIp[16] = "-";
    int     apClients = 0;
    bool    apTemp = false;      // failsafe lülitas hotspoti ajutiselt sisse
};

void begin();          // käivita vastavalt Settings-ile
void restart();        // rakenda muudetud seaded
Status status();

// Võrkude skaneerimine veebiliidesele: JSON massiiv [{"ssid":..,"rssi":..,"enc":..}]
String scanJson();

// Captive portal: DNS vastab hotspoti klientidele alati seadme IP-ga
void loop();           // kutsu perioodiliselt (failsafe: hotspot, kui võrku pole)
bool apTemporary();
bool isApAddress(uint32_t ip);  // kas IPv4 (võrgubaidijärjestuses) on hotspoti liidese IP

}  // namespace WifiMgr
