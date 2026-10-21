// =============================================================================
//  LteRate – LTE vaatajate ühine saatmiseelarve (kaadrisagedus kohandub ise)
// =============================================================================
//  Modemi UART-il pole voojuhtimist (RTS/CTS): kui ESP saadab kiiremini, kui
//  LTE üles jõuab, täitub modemi puhver ja CMUX kanal jookseb kinni. Seepärast
//  saadetakse LTE vaatajatele (RTSP, push, veebi MJPEG) kaader ainult siis, kui
//  eelarves on ruumi – kaadrisagedus = eelarve / kaadri suurus.
//
//  Eelarvet juhitakse nagu TCP-d (AIMD):
//   * modemi AT vastamisaeg (CMUX käsukanal jääb andmete taha järjekorda) –
//     kasvab enne, kui modemi puhver üle jookseb → eelarve kohe alla
//   * TCP kaadri saatmise kestus (lwIP puhver täis)
//   * kui kõik on korras ja eelarvest jääb puudu → tõstetakse aeglaselt
//  WiFi vaatajaid see ei puuduta.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace LteRate {

struct Status {
    bool     active = false;     // LTE kaudu saadeti viimase 3 s jooksul kaadreid
    float    budgetKBps = 0;     // praegune eelarve
    float    sentKBps = 0;       // tegelikult saadetud (viimased ~2 s)
    float    fps = 0;            // LTE kaudu saadetud kaadreid sekundis (kõik vaatajad kokku)
    uint32_t rttMs = 0;          // viimane modemi AT vastamisaeg
    uint32_t baseRttMs = 0;      // vastamisaeg tühikäigul
    uint32_t cuts = 0;           // mitu korda eelarvet ohu tõttu vähendati
};

void reset(uint32_t localIp, int rsrpDbm);  // PPP sai IP (0 = LTE maas); algeelarve levi järgi
bool isLteSocket(int fd);                   // kas sokli kohalik aadress on PPP IP?

bool allow(size_t bytes);                   // kas võib kaadri LTE kaudu saata (võtab eelarvest)
void consume(size_t bytes);                 // saadeti nagunii (nt heli) – arvesta eelarvest
void sent(size_t bytes, uint32_t ms);       // TCP: kaadri saatmise kestus
// UDP: paus (ms) pärast `bytes` suuruse paketi saatmist, et kaader ei jõuaks modemisse
// ühe pursakuna (UDP-l pole TCP akent – 30 kB korraga ajab modemi puhvri üle)
uint32_t paceMs(size_t bytes);

bool wantProbe();                           // kas LTE task peaks praegu AT vastamisaega mõõtma
void atSample(uint32_t rttMs, bool ok);     // AT päringu vastamisaeg (ok=false → aegus)

Status status();

}  // namespace LteRate
