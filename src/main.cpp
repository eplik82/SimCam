// =============================================================================
//  SimCam – LilyGO T-SIMCAM (ESP32-S3) LTE kaamera püsivara
// =============================================================================
//  Käivitusjärjestus:
//   1. Logi: USB Serial (115200) + mälupuhver (veebiliideses ⚙ → Logi)
//   2. Plaadi perifeeria toide (GPIO1 = HIGH)
//   3. Kaamera + autofookus (Camera_Manager)
//   4. RTSP server :554 ja veebiliides :80 (kuulavad kõigil liidestel; hakkavad
//      internetist vastama kohe, kui PPP saab IP aadressi)
//   5. LTE haldur (taustal): modem → PIN → APN → PPP → jälgimine/taastamine
//
//  Kõik moodulid töötavad oma FreeRTOS taskides; loop() prindib vaid olekut.
// =============================================================================
#include <Arduino.h>
#include <Network.h>

#include "config.h"
#include "version.h"
#include "log.h"
#include "camera_handler.h"
#include "modem_lte.h"
#include "rtsp_server.h"
#include "web_ui.h"
#include "settings.h"
#include "wifi_manager.h"
#include "ota.h"
#include "battery.h"
#include "audio.h"

static const char *TAG = "MAIN";

void setup() {
    Serial.begin(115200);
    // Oota kuni 3 s, et USB CDC jõuaks arvutiga ühenduda (logid ei kao)
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    Log::begin();                     // logipuhver (veebis /log) + eelmise käivituse logi

    LOGI(TAG, "==============================================");
    LOGI(TAG, " SimCam – LilyGO T-SIMCAM LTE kaamera");
    LOGI(TAG, " Püsivara %s", SIMCAM_VERSION);
    LOGI(TAG, " PSRAM: %u kB, flash: %u MB", ESP.getPsramSize() / 1024,
         ESP.getFlashChipSize() / (1024 * 1024));
    LOGI(TAG, "==============================================");

    // Plaadi perifeeria toide (kaamera, mPCIe) – LilyGO T-SIMCAM PWR_ON
    pinMode(BOARD_PWR_ON_PIN, OUTPUT);
    digitalWrite(BOARD_PWR_ON_PIN, HIGH);
    delay(200);

    Settings::load();                 // NVS seaded (WiFi, LTE, pildi pööre)
    Battery::begin();                 // aku pinge (GPIO3) ja olek
    Audio::begin();                   // mikrofon (I²S), kui seadetes sees

    // Kaamera enne modemit: esp_camera vajab suurt DMA/PSRAM plokki,
    // mille eraldamine on kõige kindlam kohe käivitumisel.
    if (Camera::begin()) {
        LOGI(TAG, "Kaamera OK: %s, AF: %s", Camera::sensorName(),
             Camera::afSupported() ? "jah" : "ei");
    } else {
        LOGE(TAG, "Kaamera initsialiseerimine EBAÕNNESTUS – kontrolli kaamera kaablit");
    }

    // lwIP/esp_netif TCP/IP pinu peab olema üleval enne soketite loomist
    // (tavaliselt teeb seda WiFi; siin käivitub PPP alles hiljem).
    Network.begin();
    WifiMgr::begin();                 // WiFi klient + hotspot (SimCam)

    RtspServer::begin();
    WebUI::begin();
    Ota::begin();                     // GitHubi release'ide kontroll + tagasipööramise kinnitus
    if (Settings::get().lteEnabled) LTE::begin();
    else LOGW(TAG, "LTE modem on seadetes välja lülitatud (mPCIe pessa ei saadeta midagi)");

    LOGI(TAG, "Käivitus valmis, vaba heap %u B, vaba PSRAM %u B",
         ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop() {
    // Olekulogi iga 30 s järel – aitab jälgida USB kaudu
    static uint32_t last = 0;
    if (millis() - last >= 30000) {
        last = millis();
        LTE::Status m = LTE::status();
        WifiMgr::Status w = WifiMgr::status();
        Battery::Status b = Battery::status();
        char bat[48] = "";
        if (b.enabled) snprintf(bat, sizeof(bat), " | aku %.2f V %d%% %s", b.voltage, b.percent, Battery::stateName(b.state));
        LOGI(TAG, "WiFi=%s %s (%d dBm) AP=%d kl. | LTE=%s IP=%s op=%s CSQ=%d (%d dBm) RSRP=%d | cam %.1f fps %u kB AF=%s | "
                  "rtsp=%d web=%d | heap=%u psram=%u | %.1f°C%s",
             w.staConnected ? w.staSsid : "-", w.staIp, w.staRssi, w.apClients,
             LTE::enabled() ? LTE::stateName(m.state) : "OFF", m.ip, m.op, m.csq, m.rssiDbm, m.rsrpDbm,
             Camera::fps(), (unsigned)(Camera::lastFrameBytes() / 1024), Camera::afStatus(),
             RtspServer::clients(), WebUI::streamClients(),
             ESP.getFreeHeap(), ESP.getFreePsram(), temperatureRead(), bat);
    }
    WifiMgr::loop();
    delay(10);
}
