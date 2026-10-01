// =============================================================================
//  RTSP server (port 554) – MJPEG üle RTP (RFC 2326 + RFC 2435) + heli
//  (mikrofoni sisselülitamisel: G.711 µ-law 8 kHz või L16 16 kHz, track2)
// =============================================================================
//  URL:  rtsp://<IP>:554/live
//  Transport: RTP/AVP (UDP) ja RTP/AVP/TCP (interleaved).
//  Mobiilivõrgus/NAT taga soovitame TCP-d:
//     ffplay -rtsp_transport tcp rtsp://admin:<parool>@<IP>:554/live
//     VLC: Tööriistad → Eelistused → Sisend/Koodekid → "RTP over RTSP (TCP)"
//
//  Madal viivitus: iga klient saab alati kõige värskema kaadri; kui võrk on
//  aeglane, jäetakse vahepealsed kaadrid lihtsalt vahele (puhvrit ei kogune).
// =============================================================================
#pragma once
#include <stdint.h>

namespace RtspServer {

bool begin();      // käivitab kuulava taski ja push-taski (töötab ka enne LTE ühendust)
int clients();     // aktiivsete RTSP klientide arv

// RTSP push (ANNOUNCE/RECORD) – seade saadab voo ise serverisse (nt MediaMTX).
// Seaded: Settings pushEnabled + pushUrl (rtsp://[kasutaja:parool@]host[:port]/tee).
struct PushStatus {
    char  state[12] = "off";   // off / connecting / streaming / retry / error
    char  error[80] = "";
    char  target[200] = "";    // URL ilma kasutaja/paroolita
    uint32_t since = 0;        // millis() voo algusest (0 = ei voogesita)
    uint32_t retries = 0;
    float fps = 0, kBps = 0, sentMB = 0;
};
PushStatus pushStatus();
void pushRestart();        // seaded muutusid → ühenda kohe uuesti

}  // namespace RtspServer
