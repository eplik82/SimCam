# SimCam – LilyGO T-SIMCAM kaamera (WiFi / hotspot / LTE)

ESP32-S3 püsivara LilyGO **T-SIMCAM** plaadile: autofookusega kaamera (OV5640),
**RTSP** voog, veebiliides, WiFi klient + **hotspot captive portaliga**, valikuline
**LTE** (UART-modem mPCIe pesas) ning **püsivara uuendamine otse GitHubi
release'ist (FOTA)**.

| Teenus        | Aadress                                   |
|---------------|-------------------------------------------|
| Veebiliides   | `http://<IP>/` (hotspotis `http://4.3.2.1/`, WiFi-s ka `http://simcam.local/`) |
| RTSP (MJPEG)  | `rtsp://admin:<parool>@<IP>:554/live`     |
| MJPEG voog    | `http://<IP>/stream`                       |
| Hetktõmmis    | `http://<IP>/capture`                      |
| Olek (JSON)   | `http://<IP>/api/status`                   |

> ⚠️ **MikroTik R11e-LTE selle plaadiga EI tööta ja võib rikki minna** –
> vt [Modemi ühilduvus](#modemi-ühilduvus).

---

## Omadused

* **Kaamera:** OV5640 (ka OV2640/OV3660), JPEG 800x600, pidev autofookus + nupp
  „Fookus", pööramine 0/90/180/270° (kehtib ka RTSP-s), madal viivitus – aeglase
  võrgu korral jäetakse kaadrid vahele, puhvrit ei kogune.
* **RTSP server** (port 554): RTP/JPEG (RFC 2435), UDP ja TCP interleaved,
  kuni 4 klienti. Testitud VLC ja FFmpeg-iga.
* **Veebiliides:** avalehel ainult pilt ja nupud (peata/jätka, ↺/↻ 90°,
  autofookus, hetktõmmis). Olek ja seaded eraldi lehel (⚙).
* **Juurdepääs parooliga** (veeb + RTSP), sessiooniküpsis kehtib 30 päeva.
* **WiFi klient + hotspot korraga.** Hotspotiga ühendudes avaneb telefonis
  kaamera leht ise (captive portal, testitud Samsung S24+).
* **LTE** (PPPoS, CMUX): PIN, APN, levi (CSQ/RSRP/RSRQ), automaatne taasühendus.
* **FOTA:** kontrollib GitHubi release'e, paigaldab nupuvajutusel, eelmine versioon
  taastatakse automaatselt, kui uus ei käivitu.
* Kõik seaded (WiFi, hotspot, LTE, parool, pööre) salvestatakse seadme NVS-i –
  **lähtekoodis pole paroole ega operaatori andmeid**.

## Esmane seadistamine

1. Laadi püsivara plaadile (vt [Paigaldus](#paigaldus)) või võta valmis fail
   [Releases](../../releases) lehelt.
2. Ühenda telefon WiFi võrku **SimCam**, parool **`simcam2026`**.
   Kaamera leht avaneb ise (kui mitte, ava `http://4.3.2.1/`).
3. Logi sisse parooliga **`simcam`**.
4. ⚙ → **Parool**: muuda veebi/RTSP parool.
5. ⚙ → **WiFi ja LTE seaded**:
   * WiFi klient – vali oma võrk („Otsi võrke"), sisesta parool;
   * Hotspot – muuda nimi ja parool;
   * Mobiilivõrk – APN, SIM PIN, oodatav staatiline IP (valikuline), „LTE modem sees".
6. Salvesta. WiFi rakendub kohe, LTE muudatused pärast taaskäivitust.

## Püsivara uuendamine (FOTA)

Seadmes: ⚙ → **Püsivara** → „Kontrolli uuendusi" → „Uuenda".
Seade kontrollib ka ise iga 6 tunni järel (vajab internetti WiFi või LTE kaudu)
ja näitab, kui uus versioon on saadaval. Paigaldamine toimub ainult nupuga.

* Allikas: `https://api.github.com/repos/eplik82/SimCam/releases/latest`
  (muudetav failis `src/version.h`), fail `simcam-firmware.bin`.
* HTTPS (ESP-IDF sertifikaadikogum), pilt kirjutatakse teise OTA partitsiooni ja
  kontrollitakse enne taaskäivitust.
* **Tagasipööramine:** uus püsivara märgitakse kehtivaks alles pärast 60 s
  tõrgeteta tööd. Kui see enne kokku jookseb, käivitab bootloader eelmise versiooni.

### Uue versiooni väljaandmine

```bash
git tag v1.2.0
git push origin v1.2.0
```

GitHub Actions (`.github/workflows/firmware.yml`) ehitab püsivara (versioon võetakse
sildist) ja loob release'i failidega `simcam-firmware.bin` (FOTA),
`simcam-factory.bin` (esmane USB-laadimine aadressile 0x0) ja `SHA256SUMS.txt`.

## Paigaldus

Vajalik: [PlatformIO](https://platformio.org/) (VS Code laiendus või `pip install platformio`).
Esimesel ehitusel laaditakse alla pioarduino platvorm ja Arduino-ESP32 3.x.

```bash
pio run -t upload          # kompileeri + laadi USB kaudu
pio device monitor         # logi 115200 baud
```

Kui plaat ei ilmu COM-pordina: hoia **BOOT** all, vajuta **RESET**, lase BOOT lahti.

Valmis failiga (ilma PlatformIO-ta):

```bash
esptool.py --chip esp32s3 write_flash 0x0 simcam-factory.bin
```

## Riistvara ja viigud

LilyGO T-SIMCAM (ESP32-S3R8, 8 MB OPI PSRAM, 16 MB flash). Viigud kinnitatud
LilyGO skeemi `T_SIMCAM-V1.3` ja näidete järgi (`src/config.h`):

| Funktsioon              | GPIO | Märkus                                        |
|-------------------------|------|-----------------------------------------------|
| Plaadi toide PWR_EN     | 1    | peab olema HIGH (kaamera, mPCIe)              |
| Modem PWRKEY            | 48   | skeemis PCIE_RST → mPCIe viik 6; HIGH 500 ms |
| Modem UART TX → modem   | 45   | mPCIe viik 19 (LilyGO eripesa)                |
| Modem UART RX ← modem   | 46   | mPCIe viik 17 (LilyGO eripesa)                |
| mPCIe USB D+/D−         | –    | viigud 36/38 – **ESP32-ga ühendamata**        |
| mPCIe toide             | –    | **4,2 V** (DVDD4V2), mitte standardne 3,3 V   |
| Kaamera XCLK            | 14   | 20 MHz                                        |
| Kaamera SIOD / SIOC     | 4 / 5| SCCB – ka OV5640 autofookuse juhtimine        |
| Kaamera D0…D7           | 11, 9, 8, 10, 12, 17, 16, 15 |                       |
| VSYNC / HREF / PCLK     | 6 / 7 / 13 |                                         |
| Kaamera RESET / IR      | 18   | V1.2: RESET, **V1.3: IR-filter** (`SIMCAM_BOARD_V13`) |

**Autofookus:** OV5640 läätse VCM mootorit juhib sensori sisemine mikrokontroller,
mille püsivara laaditakse SCCB kaudu (teek
[0015/ESP32-OV5640-AF](https://github.com/0015/ESP32-OV5640-AF)).

## Voo vaatamine

```bash
ffplay -rtsp_transport tcp -fflags nobuffer -flags low_delay rtsp://admin:<parool>@<IP>:554/live
```

**VLC:** Meedia → Ava võrguvoog → `rtsp://admin:<parool>@<IP>:554/live`.
Mobiilivõrgus/NAT taga: Tööriistad → Eelistused → Sisend/Koodekid → Demuxerid →
RTP/RTSP → ✔ *Use RTP over RTSP (TCP)*.

> Kui VLC näitab väikest (176x144) tühja pilti, ei saanud ta RTSP-ga ühendust
> (nt kõik 4 kliendikohta hõivatud) ja kasutas teist moodulit.

## WiFi, hotspot ja captive portal

* Klient ja hotspot töötavad korraga; RTSP ja veebiliides kuulavad kõigil liidestel.
* Hotspoti IP on meelega avalik aadress **`4.3.2.1`**: Android (sh Samsung) ei tee
  captive portal kontrolli, kui DNS vastab privaatse IP-ga (192.168.x.x).
* Seadme DNS vastab hotspoti klientidele igale nimele `4.3.2.1`-ga ja telefonide
  internetikontrollid (`generate_204`, `hotspot-detect.html`, `connecttest.txt`)
  suunatakse kaamera lehele.

## Pööramine ja kaamera juhtimine

* 180° teeb sensor ise (täiskiirus). 90°/270° puhul kodeeritakse iga kaader ümber
  (`esp_new_jpeg`, ~260 ms kaadri kohta → ~3–4 fps, pilt 600x800).
* `GET /api/cam?var=<nimi>&val=<väärtus>` – nt `rotate`, `aec`, `aec_value`,
  `agc_gain`, `brightness`, `hmirror`, `vflip`, `framesize`, `quality`, `ir`.
  Ilma parameetriteta tagastab kõik seaded.

## Ribalaius (LTE)

Modem on ESP32-ga ühendatud **UART-i** kaudu, seega piirab läbilaskevõimet UART:

| UART kiirus | Umbes kasulik  | SVGA JPEG (~15–40 kB) |
|-------------|----------------|-----------------------|
| 115200      | ~11 kB/s       | ~0,3–0,7 FPS          |
| 921600      | ~85 kB/s       | ~2–5 FPS              |
| 3000000*    | ~250 kB/s      | ~6–15 FPS             |

\* kui modem toetab (`MODEM_BAUD_TARGET`, SIM7600 toetab).

## Turvalisus

* Vaikeparoolid (`simcam` veeb/RTSP, `simcam2026` hotspot) on avalikud – **muuda need
  kohe** (seadete leht hoiatab, kuni vaikeparool on kasutusel).
* Veebiliides: sisselogimise leht → küpsis (SHA-256 seadmekohasest soolast + paroolist),
  kehtib ka pärast taaskäivitust; parooli muutmine logib teised välja.
* RTSP ja API skriptid: HTTP Basic (`curl -u admin:<parool> http://<IP>/api/status`).
* Vale parooli korral 0,8 s viivitus.
* SIM PIN on ainult kirjutatav (veebiliides seda ei näita). Kui SIM lükkab PIN-i
  tagasi, seda PIN-i enam ei proovita (kaitse PUK-lukustuse eest).

## Modemi ühilduvus

### MikroTik R11e-LTE – ei sobi

| | R11e-LTE | T-SIMCAM mPCIe pesa |
|---|---|---|
| Andmeliides | **ainult USB 2.0**: RNDIS + 2× CDC-ACM (AT) + massmälu, USB ID `2cd2:0001` | **UART** mPCIe viikudel 17/19; USB D+/D− (36/38) **pole ESP32-ga ühendatud** |
| Toide | mPCIe standard **3,3 V** (±9 %) | **4,2 V** (DVDD4V2, SIM7600 jaoks) |
| PWRKEY | puudub (standardkaart) | GPIO48 → viik 6 (standardis +1,5 V) |

1. **Ära pane R11e-LTE-d T-SIMCAM-i pessa** – 4,2 V võib kaardi rikkuda.
2. Ka õige toite korral ei saa ESP32 kaardiga suhelda (USB vs UART) – püsivaraga
   seda lahendada ei saa.

**Sobib:** LilyGO **T-PCIe SIM7600E-H** (Euroopa sagedused B1/B3/B7/B8/B20),
`MODEM_TYPE_SIM7600`, CMUX, kuni 3 Mbit/s UART. `MODEM_TYPE_GENERIC` sobib muu
UART-iga 3GPP modemiga, mis talub 4,2 V toidet.

Allikad: [MikroTik R11e-LTE](https://mikrotik.com/product/r11e_lte),
[OpenWrt #11400](https://github.com/openwrt/openwrt/issues/11400),
[T-SIMCAM skeem](https://github.com/Xinyuan-LilyGO/LilyGo-Camera-Series/tree/master/schematic),
[LilyGO T-SIMCAM wiki](https://wiki.lilygo.cc/products/t-sim-series/t-simcam/).

## Veaotsing

| Sümptom | Põhjus / lahendus |
|---|---|
| `PSRAM puudub!` | `board_build.arduino.memory_type = qio_opi` peab olema `platformio.ini`-s |
| `esp_camera_init ebaõnnestus: 0x105` | kaamera kaabel lahti või vale suunaga |
| Hotspoti leht ei avane ise | ava `http://4.3.2.1/`; lülita telefonis välja „Privaatne DNS"/VPN |
| `Modem ei vasta UART-il` | vale kaart (nt R11e-LTE) või modem pole pesas korralikult |
| `APN on seadistamata` / `PIN on seadistamata` | ⚙ → Mobiilivõrk |
| `SIM PIN vale!` | paranda PIN seadetes (sama valet PIN-i enam ei proovita) |
| `SIM on PUK lukus!` | ava SIM telefonis PUK-koodiga |
| IP ≠ oodatud | APN vale või staatilise IP teenus pole SIM-ile aktiveeritud |
| FOTA: „GitHubiga ei saanud ühendust" | seadmel pole internetti (ainult hotspot ei piisa) |
| Brownout LTE ühendumisel | modem tarbib kuni 2 A tippe – kasuta korralikku 5 V toidet |

## Projekti struktuur

```
SimCam/
├── platformio.ini            PlatformIO (pioarduino, Arduino-ESP32 3.x, 16 MB, OPI PSRAM)
├── tools/version.py          versioon git sildist → SIMCAM_VERSION
├── .github/workflows/        CI: ehitus + release (silt v*)
└── src/
    ├── config.h              viigud, pordid, vaikeseaded (ilma saladusteta)
    ├── version.h             versioon + FOTA repo
    ├── main.cpp              käivitusjärjestus + olekulogi
    ├── camera_handler.*      kaamera, autofookus, pööramine, kaadrijagur
    ├── rtsp_server.*         RTSP/RTP-JPEG server
    ├── web_ui.*, web_page.h  HTTP server, API, lehed (vaade / seaded / login)
    ├── wifi_manager.*        WiFi klient + hotspot + captive DNS + mDNS
    ├── modem_lte.*           LTE: PWRKEY, PIN, APN, PPP/CMUX, taastamine
    ├── ota.*                 FOTA GitHubi release'ist + tagasipööramine
    ├── settings.*            NVS seaded
    ├── auth.*                parool, sessiooniküpsis, HTTP Basic
    └── log.h                 logimakrod (USB Serial 115200)
```

