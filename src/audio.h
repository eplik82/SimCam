// =============================================================================
//  Mikrofon (MSM261S4030H0R, I²S) – heli RTSP-sse ja brauserisse
//
//  Mikrofon loetakse 16 kHz / 24 bit, eemaldatakse alalisvool, rakendatakse
//  võimendus ja kirjutatakse 16-bit PCM ringpuhvrisse (PSRAM, 4 s). Tarbijad
//  (RTSP kliendid, /audio) loevad puhvrist oma positsioonilt; aeglase võrgu
//  korral hüpatakse ette, et viivitus ei koguneks.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace Audio {

void begin();                 // käivitab, kui seadetes sees
void apply();                 // seaded muutusid (sees/väljas, võimendus)
bool running();

// Positsioon = kirjutatud 16 kHz diskreetide arv algusest
uint32_t position();
// Loe täpselt n diskreeti alates *pos (kui olemas). Kui *pos on puhvrist välja
// jäänud või maha jäänud rohkem kui maxLagMs, hüpatakse ette. Tagastab false,
// kui n diskreeti pole veel saadaval. *skipped = mitu diskreeti vahele jäeti.
bool read(uint32_t *pos, int16_t *dst, size_t n, uint32_t maxLagMs, uint32_t *skipped);
// Millal (millis) diskreet `pos` salvestati – A/V sünkroniseerimiseks
uint32_t msAt(uint32_t pos);

float levelDb();              // RMS viimase ~0,2 s jooksul, dBFS (-90…0)
float peakDb();               // tipp viimase ~0,2 s jooksul, dBFS
int   channel();              // kumba I²S kanalit kasutatakse (0 = vasak, 1 = parem)
// Mõlema kanali signaalitase (dBFS, alalisvool eemaldatud, enne võimendust) –
// mikrofon annab signaali ainult ühes kanalis; teine on hõljuv (juhuslik sahin
// või konstant). Õige kanali leiab, kui rääkida ja vaadata, kumb tase muutub.
float chanDb(int ch);

// Kodeerijad
uint8_t mulaw(int16_t s);                                     // G.711 µ-law
size_t encodePcmu(const int16_t *in16k, size_t n, uint8_t *out); // 16 kHz → 8 kHz µ-law

}  // namespace Audio
