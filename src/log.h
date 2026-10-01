// =============================================================================
//  Logimine: USB Serial (115200) + mälupuhver, mida näeb veebiliideses (/log).
//
//  * Iga rida: [millis][tase][moodul] tekst – läheb USB Seriali ja RAM-i
//    ringpuhvrisse (PSRAM, LOG_BUF_SIZE). Puhvrist loetakse /api/log kaudu.
//  * ESP-IDF komponentide (WiFi, PPP, esp_modem, httpd …) hoiatused ja vead
//    püütakse samuti puhvrisse (moodul "IDF").
//  * Viimased LOG_RTC_SIZE baiti hoitakse ka RTC mälus, mis jääb alles
//    tarkvaralisel taaskäivitusel, kokkujooksmisel ja watchdogi korral →
//    pärast krahhi näeb eelmise käivituse viimaseid ridu (/api/log?prev=1).
//  * Kõik funktsioonid on lõimekindlad (rekursiivne mutex).
// =============================================================================
#pragma once
#include <Arduino.h>

#define LOG_BUF_SIZE   (64 * 1024)   // jooksva käivituse logi (PSRAM)
#define LOG_RTC_SIZE   (3 * 1024)    // eelmise käivituse lõpp (RTC mälu)

namespace Log {

// Käivita võimalikult vara (kohe pärast Serial.begin): loeb eelmise käivituse
// logi RTC mälust, eraldab puhvri ja ühendab ESP-IDF logi.
void begin();

void write(const char *lvl, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Kopeeri jooksva logi baidid alates positsioonist `since` (0 = algusest).
// Tagastab kopeeritud baitide arvu; *next = järgmise päringu positsioon.
// Kui `since` on juba üle kirjutatud, alustatakse vanimast tervest reast.
size_t read(uint32_t since, char *dst, size_t max, uint32_t *next);
uint32_t position();                 // kõigi kirjutatud baitide arv
void clear();                        // tühjenda jooksev logi (vaates)

const char *previous(size_t *len);   // eelmise käivituse logi lõpp (või "")
const char *resetReason();           // viimase taaskäivituse põhjus (eesti k.)
const char *resetCode();             // sama ASCII koodina (POWERON, SW, PANIC …)
uint32_t warnings();                 // W ridu alates käivitusest
uint32_t errors();                   // E ridu alates käivitusest

}  // namespace Log

#define LOG_(lvl, tag, fmt, ...) Log::write(lvl, tag, fmt, ##__VA_ARGS__)

#define LOGI(tag, fmt, ...) LOG_("I", tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) LOG_("W", tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) LOG_("E", tag, fmt, ##__VA_ARGS__)
