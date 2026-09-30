// =============================================================================
//  FOTA – püsivara uuendamine GitHubi release'ist
// =============================================================================
//  * Kontrollib https://api.github.com/repos/<repo>/releases/latest
//    (esimest korda ~1 min pärast käivitust, siis iga OTA_CHECK_HOURS tunni järel
//    või nupuga "Kontrolli uuendusi").
//  * Kui release'i versioon (silt v1.2.3) on uuem kui praegune, näitab
//    veebiliides "Uuendus saadaval". Paigaldamine nupuga "Uuenda" või, kui
//    seadetes on "Uuenda automaatselt" sees, kohe pärast kontrolli.
//  * Allalaadimine HTTPS-iga (ESP-IDF sertifikaadikogum), pilt kirjutatakse
//    teise OTA partitsiooni ja kontrollitakse enne taaskäivitust.
//  * Tagasipööramine: uus püsivara märgitakse kehtivaks alles pärast 60 s
//    tõrgeteta tööd. Kui see enne jookseb kokku, taastab bootloader eelmise.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace Ota {

void begin();          // käivitab taustataski
void checkNow(uint32_t delayMs = 0);   // asünkroonne kontroll (valikulise viivitusega)
bool startUpdate();    // asünkroonne uuendamine (false, kui uuendust pole)
String statusJson();   // olek veebiliidesele

}  // namespace Ota
