// =============================================================================
//  RTSP server (port 554) – MJPEG üle RTP (RFC 2326 + RFC 2435)
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

namespace RtspServer {

bool begin();      // käivitab kuulava taski (töötab ka enne LTE ühendust)
int clients();     // aktiivsete RTSP klientide arv

}  // namespace RtspServer
