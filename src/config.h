// =============================================================================
//  SimCam – keskne konfiguratsioon
//  Kõik viigud, võrgu- ja kaameraseaded on siin ühes kohas.
// =============================================================================
#pragma once

#include <Arduino.h>
#include "esp_camera.h"

// -----------------------------------------------------------------------------
//  Mobiilivõrk / SIM
// -----------------------------------------------------------------------------
// SIM PIN, APN ja oodatav staatiline IP EI ole koodis – need sisestatakse
// esmakordsel seadistamisel veebiliideses (⚙ → Mobiilivõrk) ja salvestatakse
// seadme NVS-i. Nii võib lähtekood olla avalik.

// -----------------------------------------------------------------------------
//  Modemi tüüp  –  LOE README "Modemi ühilduvus"!
// -----------------------------------------------------------------------------
// T-SIMCAM mPCIe pesa on LilyGO eripesa: modemiga suheldakse UART-i kaudu
// (mPCIe viigud 17/19) ja toide on 4,2 V. Sobivad ainult UART-iga kaardid,
// nt LilyGO T-PCIe SIM7600E-H / SIM7600G-H.
//
// MikroTik R11e-LTE EI SOBI: see on ainult USB-seade (RNDIS + CDC-ACM, USB ID
// 2cd2:0001) ja vajab 3,3 V toidet. T-SIMCAM-i mPCIe USB liinid pole ESP32-ga
// ühendatud ning 4,2 V toide ületab mPCIe standardi (3,3 V ±9 %).
// Sierra Wireless MC7304 EI SOBI samal põhjusel: ainult USB 2.0 (QMI/MBIM,
// AT-pordid USB kaudu), viigud 17/19 ühendamata, toide 3,3 V.
#define MODEM_TYPE_SIM7600   1        // LilyGO T-PCIe SIM7600 (soovitatud)
#define MODEM_TYPE_GENERIC   2        // muu UART-iga 3GPP modem
#define MODEM_TYPE           MODEM_TYPE_SIM7600

// -----------------------------------------------------------------------------
//  T-SIMCAM viigud (kinnitatud LilyGO skeemi T_SIMCAM-V1.3 ja näidete järgi)
// -----------------------------------------------------------------------------
// Plaadi versioon on trükitud plaadile. V1.3: GPIO18 = IR-filter, kaamera
// RESET-i ei ole. V1.2: GPIO18 = kaamera RESET.
#define SIMCAM_BOARD_V13     1        // 1 = V1.3, 0 = V1.2

// Plaadi perifeeria toide – peab olema HIGH enne kaamerat/modemit
#define BOARD_PWR_ON_PIN     1

// mPCIe pesa (modem)
#define MODEM_PWRKEY_PIN     48   // skeemis "PCIE_RST" → mPCIe viik 6; LilyGO
                                  // T-PCIe kaardil PWRKEY (HIGH-impulss 500 ms)
#define MODEM_TX_PIN         45   // ESP32 TX → mPCIe viik 19 (modemi RXD)
#define MODEM_RX_PIN         46   // ESP32 RX ← mPCIe viik 17 (modemi TXD)
#define MODEM_RESET_PIN      -1   // PERST# (viik 22) pole ühendatud
#define MODEM_LED_PIN        21   // mPCIe viik 42 LED_WWAN (sisend modemilt)
#define MODEM_UART_NUM       1

// Kaamera (DVP 8-bit) – LilyGO T-SIMCAM
#define CAM_PWDN_PIN         -1
#if SIMCAM_BOARD_V13
#define CAM_RESET_PIN        -1
#define CAM_IR_PIN           18   // IR-filtri lüliti (V1.3)
#else
#define CAM_RESET_PIN        18
#define CAM_IR_PIN           -1
#endif
#define CAM_XCLK_PIN         14
#define CAM_SIOD_PIN         4    // SCCB/I2C SDA (ka OV5640 AF mootori juhtimine)
#define CAM_SIOC_PIN         5    // SCCB/I2C SCL
#define CAM_Y9_PIN           15   // D7
#define CAM_Y8_PIN           16   // D6
#define CAM_Y7_PIN           17   // D5
#define CAM_Y6_PIN           12   // D4
#define CAM_Y5_PIN           10   // D3
#define CAM_Y4_PIN           8    // D2
#define CAM_Y3_PIN           9    // D1
#define CAM_Y2_PIN           11   // D0
#define CAM_VSYNC_PIN        6
#define CAM_HREF_PIN         7
#define CAM_PCLK_PIN         13

// Muu (hetkel kasutamata, dokumentatsiooniks)
#define BUTTON_PIN           0
#define SD_CS_PIN            47
#define SD_SCLK_PIN          39
#define SD_MOSI_PIN          38
#define SD_MISO_PIN          40

// Aku (skeem T_SIMCAM-V1.3: TP4056 laadija, jagur RD2/RD1 = 100 k / 100 k)
#define BAT_ADC_PIN          3    // BAT_ADC → ESP32-S3 GPIO3 (ADC1_CH2)
#define BAT_DIVIDER          2.0f // aku pinge = ADC pinge × 2

// Mikrofon MSM261S4030H0R (I²S MEMS, skeem T_SIMCAM-V1.3 U3, L/R = GND → vasak kanal)
#define MIC_SCK_PIN          41   // I2S_SCK (BCLK)
#define MIC_WS_PIN           42   // I2S_WS (LRCLK)
#define MIC_SD_PIN           2    // I2S_SDO (mikrofoni andmed → ESP32)
#define MIC_RATE             16000 // Hz – sisemine diskreetimissagedus
#define AUDIO_HTTP_MAX       2    // samaaegsed brauseri helivood (/audio)

// -----------------------------------------------------------------------------
//  Modemi UART / PPP
// -----------------------------------------------------------------------------
// PPP läbilaskevõime on piiratud UART kiirusega (~10 kB/s 1 Mbit/s kohta).
// Modem käivitub tavaliselt 115200 baudiga; püsivara proovib seejärel lülitada
// kiiremale (AT+IPR). Kui modem sihtkiirust ei toeta, jäädakse 115200 peale.
// SIM7600 toetab ka 3000000 ja 3686400 – proovi kiirema pildi jaoks 3000000.
#define MODEM_BAUD_DEFAULT   115200
#define MODEM_BAUD_TARGET    921600
// CMUX lubab AT-käske (RSSI, operaator) saata ka PPP ühenduse ajal.
// Kui modem CMUX-i ei toeta, minnakse automaatselt tavalisse DATA režiimi.
#define MODEM_USE_CMUX       1

#define LTE_ATTACH_TIMEOUT_MS   180000   // võrku registreerumise ooteaeg
#define LTE_IP_TIMEOUT_MS        60000   // PPP IP aadressi ooteaeg
#define LTE_MONITOR_PERIOD_MS    10000   // levi/staatuse päringute intervall
#define LTE_LINK_LOST_GRACE_MS   30000   // kui kaua IP võib puududa enne taastamist
#define LTE_REBOOT_AFTER_MS    900000UL  // 15 min ilma ühenduseta → ESP restart

// -----------------------------------------------------------------------------
//  Kaamera
// -----------------------------------------------------------------------------
// Resolutsioon: SVGA (800x600) on UART-PPP jaoks hea kompromiss.
// Võimalikud: FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_XGA, FRAMESIZE_HD,
//             FRAMESIZE_UXGA, FRAMESIZE_FHD (1080p, ainult OV5640)
#define CAM_FRAME_SIZE       FRAMESIZE_SVGA   // vaikimisi; muudetav veebis (⚙ → Pilt)
// Kaadripuhvrid eraldatakse käivitusel selle (või sensori maksimumi) jaoks, et
// resolutsiooni saaks hiljem suurendada. FHD (1920×1080) on ka RTP/JPEG
// piirist (2040 px) väiksem; OV2640 puhul piirab sensor UXGA-ga.
#define CAM_FRAME_SIZE_MAX   FRAMESIZE_FHD
#define CAM_JPEG_QUALITY     14          // 0–63, väiksem = parem kvaliteet/suurem fail
#define CAM_XCLK_HZ          20000000    // OV5640/OV2640 jaoks 20 MHz
#define CAM_FB_COUNT         2           // topeltpuhver PSRAM-is
#define CAM_MAX_FPS          15          // hõivatud kaadrite ülempiir
#define CAM_IDLE_SLEEP_MS    15000       // nii kaua ilma vaatajata → andur ooterežiimi (aku)
#define CAM_WAKE_DROP_MS     400         // ärkamisel jäetakse kaadrid vahele (säri kohandub)
#define MIC_IDLE_STOP_MS     10000       // mikrofon peatub nii kaua pärast viimast vaatajat/kuulajat
// Autofookuse režiim (seadetes muudetav): ühekordne täisotsing käivitusel ja
// "Fookus" nupuga (lääts jääb paigale), pidev AF või käsitsi läätse asend.
#define AF_MODE_SINGLE       0
#define AF_MODE_CONT         1
#define AF_MODE_MANUAL       2
#define AF_MODE_DEFAULT      AF_MODE_SINGLE
#define AF_POS_DEFAULT       512         // käsitsi fookus 0 (lõpmatus) … 1023 (lähedal)
// Pildi pööre (vaikimisi; muudetav veebiliidesest): 0 või 180 (teeb sensor ise)
#define CAM_ROTATION_DEFAULT 0
#define CAM_MIRROR_DEFAULT   1           // T-SIMCAM OV5640 annab muidu peegelpildi (tekst tagurpidi)

// -----------------------------------------------------------------------------
//  Serverid
// -----------------------------------------------------------------------------
#define RTSP_PORT            554
#define RTSP_PATH            "/live"
#define RTSP_MAX_CLIENTS     4
#define RTSP_RTP_MAX_PAYLOAD 1400        // < PPP MTU (1500) – IP/UDP päised
#define RTSP_SESSION_TIMEOUT 60          // s, UDP klientidel keep-alive nõutud

#define HTTP_PORT            80
#define HTTP_MAX_STREAMS     2           // samaaegsed MJPEG vaatajad

// -----------------------------------------------------------------------------
//  Juurdepääsu kaitse – veebiliides (sisselogimine) ja RTSP (HTTP Basic)
// -----------------------------------------------------------------------------
// Parool on muudetav veebiliidese seadetes (salvestatakse NVS-i).
//   RTSP: rtsp://admin:<parool>@<IP>:554/live
#define AUTH_USER            "admin"
#define WEB_PASS_DEFAULT     "simcam"
#define CAM_NAME_DEFAULT     "SimCam"    // kaamera nimi (lehtede päis, vahekaart, RTSP)

// -----------------------------------------------------------------------------
//  WiFi (vaikeseaded – muudetavad veebiliidesest, salvestatakse NVS-i)
// -----------------------------------------------------------------------------
// Klient (STA): seade ühendub olemasoleva WiFi võrguga.
// Esmakordsel käivitusel on klient väljas – ühendu hotspotiga ja seadista
// WiFi võrk veebiliideses (⚙ → WiFi ja LTE seaded).
#define WIFI_STA_ENABLED_DEFAULT  0
#define WIFI_STA_SSID_DEFAULT     ""
#define WIFI_STA_PASS_DEFAULT     ""
// Hotspot (AP): telefon/sülearvuti ühendub otse seadmega → http://4.3.2.1
// NB! Hotspoti IP on meelega AVALIK aadress (4.3.2.1), mitte 192.168.x.x:
// Android (sh Samsung) ei tee captive portal kontrolli, kui DNS vastab
// privaatse IP-ga, ja siis ei avane kaamera leht automaatselt.
#define WIFI_AP_IP_BYTES          4, 3, 2, 1
#define WIFI_AP_IP_STR            "4.3.2.1"
#define WIFI_AP_ENABLED_DEFAULT   1
#define WIFI_AP_SSID_DEFAULT      "SimCam"
#define WIFI_AP_PASS_DEFAULT      "simcam2026"   // WPA2, vähemalt 8 märki
#define WIFI_AP_CHANNEL           6              // STA ühenduse korral järgib ruuteri kanalit
#define WIFI_HOSTNAME             "simcam"       // http://simcam.local (mDNS)

// LTE modemi haldur. 0 = välja lülitatud (ei saadeta PWRKEY impulsse ega
// AT-käske). NB! MikroTik R11e-LTE / Sierra MC7304 puhul hoia väljas ja eemalda kaart pesast.
#define LTE_ENABLED_DEFAULT       0
