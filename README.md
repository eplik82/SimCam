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
| Heli (brauser)| `http://<IP>/audio` (mikrofoni sisselülitamisel) |
| Hetktõmmis    | `http://<IP>/capture`                      |
| Olek (JSON)   | `http://<IP>/api/status`                   |
| Logi          | `http://<IP>/log` (tekstina `http://<IP>/api/log`) |

> ⚠️ **MikroTik R11e-LTE ja Sierra Wireless MC7304 (ning teised ainult USB-ga
> mPCIe modemid) selle plaadiga EI tööta ja võivad rikki minna** –
> vt [Modemi ühilduvus](#modemi-ühilduvus).

---

## Omadused

* **Kaamera:** OV5640 (ka OV2640/OV3660), JPEG, resolutsioon valitav 320×240 … 1920×1080
  (vaikimisi 800×600, ⚙ → 📷 Pilt), pidev autofookus + nupp
  „Fookus", pööramine 0°/180° (teeb sensor, kehtib ka RTSP-s), madal viivitus –
  aeglase võrgu korral jäetakse kaadrid vahele, puhvrit ei kogune.
* **Pealeht:** pildi all tegelik kaadrisagedus ja võrgukiirus (kB/s, Mbit/s), mida
  just see brauser saab.
* **RTSP server** (port 554): RTP/JPEG (RFC 2435), UDP ja TCP interleaved,
  kuni 4 klienti. Testitud VLC ja FFmpeg-iga.
* **Veebiliides:** avalehel ainult pilt ja nupud (peata/jätka, 0°/180°,
  autofookus, hetktõmmis). Olek ja seaded eraldi lehel (⚙).
* **Juurdepääs parooliga** (veeb + RTSP), sessiooniküpsis kehtib 30 päeva.
* **WiFi klient + hotspot korraga.** Hotspotiga ühendudes avaneb telefonis
  kaamera leht ise (captive portal, testitud Samsung S24+).
* **LTE** (PPPoS, CMUX): PIN, APN, levi (CSQ/RSRP/RSRQ), automaatne taasühendus,
  SIM-kaardi PIN-koodi muutmine veebiliidesest (`AT+CPWD`).
* **Failsafe:** kui WiFi klient ja LTE on seadetes mõlemad väljas, jääb hotspot alati
  sisse; kui seadmel pole 2 minutit ühtegi võrguühendust, lülitub hotspot ajutiselt
  ise sisse (ja pärast ühenduse taastumist välja).
* **FOTA:** kontrollib GitHubi release'e, paigaldab nupuvajutusel või soovi korral
  automaatselt; eelmine versioon taastatakse, kui uus ei käivitu.
* **Mikrofon** (MSM261S4030H0R, I²S): heli RTSP voos (G.711 8 kHz või L16 16 kHz)
  ja brauseris (🔊), helitaseme näit, võimendus – vaikimisi **väljas**, vt [Mikrofon](#mikrofon).
* **Aku** (TP4056 laadija plaadil): täituvus %, pinge, olek (laeb / tühjeneb /
  täis), hinnanguline tööaeg ja 24 h pingegraafik – vt [Aku](#aku).
* **Logi veebiliideses** (⚙ → 📄 Logi): seadme logi reaalajas, filtreerimine
  taseme ja teksti järgi, allalaadimine; pärast kokkujooksmist ka **eelmise
  käivituse viimased read** ja taaskäivituse põhjus – vt [Logi](#logi).
* Kõik seaded (WiFi, hotspot, LTE, parool, pööre) salvestatakse seadme NVS-i –
  **lähtekoodis pole paroole ega operaatori andmeid**.

## Esmane seadistamine

1. Paigalda püsivara: Windowsi `simcam-flasher-<versioon>.exe` või brauseris
   <https://eplik82.github.io/SimCam/> (vt [Paigaldus](#paigaldus)).
2. Ühenda telefon WiFi võrku **SimCam**, parool **`simcam2026`**.
   Kaamera leht avaneb ise (kui mitte, ava `http://4.3.2.1/`).
3. Logi sisse parooliga **`simcam`**.
4. ⚙ → **Parool**: muuda veebi/RTSP parool.
5. ⚙ → **WiFi ja LTE seaded**:
   * WiFi klient – vali oma võrk („Otsi võrke"), sisesta parool;
   * Hotspot – muuda nimi ja parool;
   * Mobiilivõrk – APN, SIM PIN, „LTE modem sees" (operaatorilt saadud IP kuvatakse seal).
6. Salvesta. WiFi rakendub kohe, LTE muudatused pärast taaskäivitust.

## Püsivara uuendamine (FOTA)

Seadmes: ⚙ → **Püsivara** → „Kontrolli uuendusi" → „Uuenda".
Seade kontrollib ka ise 1 min pärast käivitust ja siis iga 6 tunni järel (vajab
internetti WiFi või LTE kaudu) ja näitab, kui uus versioon on saadaval.

**Uuenda automaatselt** (märkeruut samal kaardil, vaikimisi väljas): leitud uus
versioon paigaldatakse kohe ise. Kui uus versioon ei käivitu ja bootloader pöörab
tagasi, jätab seade selle versiooni meelde ega proovi seda automaatselt uuesti
(käsitsi „Uuenda" nupuga saab ikka proovida).

* Allikas: `https://api.github.com/repos/eplik82/SimCam/releases/latest`
  (muudetav failis `src/version.h`), fail `simcam-firmware.bin`.
* HTTPS (ESP-IDF sertifikaadikogum), pilt kirjutatakse teise OTA partitsiooni ja
  kontrollitakse enne taaskäivitust.
* **Tagasipööramine:** uus püsivara märgitakse kehtivaks alles pärast 60 s
  tõrgeteta tööd. Kui see enne kokku jookseb, käivitab bootloader eelmise versiooni.

### Uue versiooni väljaandmine

```bash
git tag v1.8.4
git push origin v1.8.4
```

GitHub Actions (`.github/workflows/firmware.yml`) ehitab püsivara (versioon võetakse
sildist) ja loob release'i failidega `simcam-firmware.bin` (FOTA),
`simcam-factory.bin` (esmane USB-laadimine aadressile 0x0),
`simcam-flasher-<versioon>.exe` (Windowsi paigaldaja) ja `SHA256SUMS.txt`
ning avaldab brauseri-paigalduslehe (GitHub Pages). Sildi asemel võib kasutada ka
Actions → Firmware → Run workflow (sisend `tag`).

## Paigaldus

### Uus seade – ilma arenduskeskkonnata

**Windows (.exe):** laadi [Releases](../../releases/latest) lehelt
`simcam-flasher-<versioon>.exe`, ühenda T-SIMCAM USB-C kaabliga ja käivita fail.
Programm leiab plaadi pordi ise (Espressif USB, VID `303A`), küsib, kas flash
kustutada (uuel plaadil: jah), ja paigaldab püsivara (see on .exe-s sees).
Allkirjastamata programmi puhul näitab Windows SmartScreen hoiatust →
„Rohkem teavet" → „Käivita ikkagi".

**Brauserist (Chrome/Edge):** <https://eplik82.github.io/SimCam/> → „Paigalda SimCam"
→ vali plaadi port → „Erase device" (uuel plaadil). Kasutab
[ESP Web Tools](https://esphome.github.io/esp-web-tools/)-i (Web Serial); leht
avaldatakse automaatselt koos release'iga, kui release tehakse `main` harust või
sildist ja repos on Pages sisse lülitatud (Settings → Pages → Source:
**GitHub Actions**).

**macOS / Linux:** `pip install esptool==4.8.1` ja
`python tools/flasher/simcam_flasher.py simcam-factory.bin` (või otse
`esptool.py --chip esp32s3 write_flash 0x0 simcam-factory.bin`).

Kui plaat ei ilmu pordina või ühendus ei õnnestu: hoia **BOOT** all, vajuta
**RESET**, lase BOOT lahti ja proovi uuesti; pärast paigaldust vajuta RESET.
Edasi: [Esmane seadistamine](#esmane-seadistamine); hilisemad uuendused tulevad
üle võrgu (FOTA).

### Lähtekoodist (PlatformIO)

Vajalik: [PlatformIO](https://platformio.org/) (VS Code laiendus või `pip install platformio`).
Esimesel ehitusel laaditakse alla pioarduino platvorm ja Arduino-ESP32 3.x.

```bash
pio run -t upload          # kompileeri + laadi USB kaudu
pio device monitor         # logi 115200 baud
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

> Kui VLC ütleb „authentication failed" või näitab väikest (176x144) tühja pilti,
> puudub URL-ist parool (`rtsp://admin:<parool>@…`) või on kõik 4 kliendikohta hõivatud.

## WiFi, hotspot ja captive portal

* Klient ja hotspot töötavad korraga; RTSP ja veebiliides kuulavad kõigil liidestel.
* Hotspoti IP on meelega avalik aadress **`4.3.2.1`**: Android (sh Samsung) ei tee
  captive portal kontrolli, kui DNS vastab privaatse IP-ga (192.168.x.x).
* Seadme DNS vastab hotspoti klientidele igale nimele `4.3.2.1`-ga ja telefonide
  internetikontrollid (`generate_204`, `hotspot-detect.html`, `connecttest.txt`)
  suunatakse kaamera lehele.

## Pööramine ja kaamera juhtimine

**Resolutsioon:** ⚙ → **📷 Pilt** → QVGA 320×240, VGA 640×480, **SVGA 800×600**
(vaikimisi), XGA 1024×768, HD 1280×720, SXGA 1280×1024, UXGA 1600×1200 või
Full HD 1920×1080 (OV2640 puhul kuni UXGA). Valik rakendub kohe ja salvestub.
Suurem resolutsioon = väiksem kaadrisagedus ja suurem andmemaht – LTE-s soovitame
SVGA-d või väiksemat. Kaadripuhvrid eraldatakse käivitusel suurima resolutsiooni
jaoks.

* Pööre: 0° või 180° – teeb sensor ise (täiskiirus, lisamälu pole vaja).
  90°/270° (tarkvaraline JPEG ümberkodeerimine) eemaldati v1.8.0-s mälu ja
  protsessoriaja säästmiseks; varem salvestatud 270° muutub 180°-ks, 90° → 0°.
* `GET /api/cam?var=<nimi>&val=<väärtus>` – nt `rotate`, `aec`, `aec_value`,
  `agc_gain`, `brightness`, `hmirror`, `vflip`, `framesize`, `quality`, `ir`.
  Ilma parameetriteta tagastab kõik seaded.

## Mikrofon

Plaadil on digitaalne MEMS-mikrofon **MSM261S4030H0R** (skeem `T_SIMCAM-V1.3`,
U3; takt = GPIO41, andmed = GPIO2, L/R = GND; GPIO42 (WS) jääb kasutamata – samad
viigud on LilyGO tehasetarkvaras). **NB:** kuigi skeemil on I²S mikrofon, annab plaadil
olev kiip **PDM-voo** (andmed ainult ühel taktipoolel, teisel kõrgtakistuslik). I²S
STD režiimis loeti seepärast ainult nulle või täismahus sahinat; püsivara loeb nüüd
ESP32-S3 riistvaralise PDM→PCM muunduriga (v1.8.5). Mikrofon on vaikimisi **väljas**:
⚙ → **🎤 Mikrofon** → „Mikrofon sees".

| Seade | Tähendus |
|---|---|
| Helitase | RMS ja tipp (dBFS) viimase 0,2 s jooksul; punane = tipp üle −3 dBFS (moonutab) |
| Võimendus | 0–40 dB (vaikimisi 24 dB). Vaikne heli → suurenda; tipp punane → vähenda |
| Helikvaliteet | **G.711 µ-law 8 kHz** (64 kbit/s, soovitatav, toetavad kõik mängijad ja salvestid) või **L16 16 kHz** (256 kbit/s, selgem) |
| Heli RTSP voos | lisab RTSP-sse helirajad (`track2`); rakendub uutele ühendustele |
| Mikrofoni kanal | **automaatne** (vaikimisi), vasak või parem. Kaardil on mõlema kanali tase – õige on see, mille tase rääkides muutub; vale kanal annab ainult sahinat |

* **Brauseris:** avalehel 🔈/🔊 nupp (heli algab alles vajutusel – brauserid ei luba
  heli automaatselt), pildi all helitaseme riba; viivitus ~0,2–0,4 s. Seadete lehel
  „🔈 Kuula" mikrofoni testimiseks. Samaaegselt kuni 2 kuulajat.
* **RTSP:** sama URL (`rtsp://admin:<parool>@<IP>:554/live`) – VLC/ffplay mängivad
  pilti ja heli koos. Pilt ja heli seotakse ühisele ajateljele RTCP Sender Reportiga
  (iga 5 s). Aeglases võrgus jäetakse üle 0,4 s maha jäänud heli vahele.
* PDM takt on 16 kHz × 128 = 2,048 MHz. PDM-i stereo kahest poolest on üks mikrofon
  (vaikses ruumis ~−70 dBFS) ja teine konstantne (−120 dBFS); automaatrežiim valib
  aktiivse. Käivitusel logitakse (`MIC`) mõlema kanali tase.
* Heli töödeldakse: PDM → 16 kHz PCM (riistvaras), alalisvoolu eemaldus, võimendus.
* Heli salvestamisel arvesta teiste inimeste privaatsusega.

## Aku

T-SIMCAM V1.3 plaadil on Li-ion aku pistik (P2), **TP4056** laadija (laadimisvool
~600 mA, PROG = 2 kΩ; laeb USB-C toitest) ja aku pinge jagur 100 k / 100 k →
**GPIO3** (`BAT_ADC`).

Veebiliideses: ⚙ → **🔋 Aku**, avalehel pildi all `🔋 63 %` (`⚡` laadimisel,
`🪫` madal).

| Näit | Kuidas saadakse |
|---|---|
| Pinge | GPIO3 ADC (16 lugemise keskmine iga 2 s, silutud), × 2 jaguri järgi, × kalibreerimistegur |
| Täituvus % | Li-ion tühjenemiskõvera järgi koormuse all (4,15 V = 100 %, 3,30 V = 0 %); tühjenemisel näit ainult langeb |
| Olek | pinge tõuseb (10 min) → **Laeb**; täituvus langeb (kuni 60 min) → **Tühjeneb** (≤ 15 % → **Madal**); muidu ≥ 4,15 V → **Täis / laadijal** |
| Tööaeg | allesolev % ÷ täituvuse kulu (%/min) viimase kuni 60 min jooksul pärast laadimist; esimene hinnang ~20 min pärast laadijast eemaldamist |
| Graafik | punkt iga 30 s järel, viimased 24 h (1 h / 6 h / 24 h vaade) |
| USB arvutiga | ESP32 USB näeb arvutit (siis aku laeb); tavaline USB-laadija ei paista |

**Voolu ei saa mõõta:** plaadil pole voolu mõõtmist ning TP4056 oleku viigud
(CHRG/STDBY) on ühendatud ainult plaadi LED-iga, mitte ESP32-ga. Seepärast
tuletatakse olek pinge muutumisest – esimese ~3 min jooksul on olek „Mõõdan…".
Laadimise ajal on pinge (ja %) tegelikust kõrgem. Voolu ja võimsuse mõõtmiseks
saab akujuhtmesse lisada nt INA219/INA226 mooduli (vajab I²C viike ja tarkvara
tuge).

**Kalibreerimine:** ESP32 ADC viga on mõni protsent. Mõõda aku pinge
multimeetriga ja sisesta see ⚙ → Aku → „Aku seaded ja kalibreerimine" →
„Kalibreeri". Seal saab aku jälgimise ka välja lülitada (kui akut pole).
Ilma akuta näitab laadija väljund ~4,2 V, s.t „Täis".

API: `GET /api/battery` (olek + ajalugu mV-des), olek ka `/api/status` → `bat`;
`POST /api/battery` väljadega `en=0|1`, `v=<volti>` (kalibreerimine), `reset_cal=1`.

## Logi

Seadme logi näeb ilma USB-kaablita: ⚙ → **📄 Logi** (`http://<IP>/log`).

* **See käivitus** – viimased ~64 kB logiridu (PSRAM-is ringpuhver, vanemad read
  kirjutatakse üle). Uued read tulevad juurde iga 2 s järel; ⏸ peatab.
* **Eelmine käivitus** – viimased ~3 kB eelmise käivituse logist. Hoitakse RTC
  mälus, mis jääb alles tarkvaralise taaskäivituse, püsivara uuenduse,
  kokkujooksmise ja watchdogi korral (toite kadumisel kustub).
* **Taaskäivituse põhjus** (toide, tarkvaraline, KOKKUJOOKSMINE, brownout …) ning
  hoiatuste/vigade arv on näha ka seadete lehel (⚙ → Süsteem).
* Filtrid: kõik / hoiatused + vead / ainult vead ning tekstiotsing. 🕒 näitab ridade
  juures kellaaega (arvutatud brauseri kellast, seadmel endal kella pole).
* **⬇ Laadi alla** salvestab logi failina (`simcam-<versioon>-logi.log`).
* Logisse jõuavad kõigi moodulite read (`MAIN`, `CAM`, `WIFI`, `LTE`, `RTSP`, `WEB`,
  `OTA` …) ja ESP-IDF komponentide hoiatused/vead (`IDF`). Sama logi tuleb endiselt
  ka USB Seriali (115200).
* Skriptidele: `curl -u admin:<parool> "http://<IP>/api/log"` (kogu logi),
  `?since=<N>` – ainult uued read (N = eelmise vastuse päis `X-Log-Next`),
  `?prev=1` – eelmine käivitus, `POST /api/log/clear` – tühjenda.

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
  RTSP paroolinõude saab kohtvõrgu jaoks välja lülitada (⚙ → RTSP ja parool) – siis
  töötab ka `rtsp://<IP>:554/live`.
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

### Sierra Wireless AirPrime MC7304 – ei sobi

MC7304 on hea LTE Cat 3 kaart (Euroopa sagedused B1/B3/B7/B8/B20), kuid sama
põhjusel nagu R11e-LTE ei saa ESP32 sellega T-SIMCAM-i pesas suhelda:

| | MC7304 | T-SIMCAM mPCIe pesa |
|---|---|---|
| Andmeliides | **ainult USB 2.0** (QMI/MBIM andmeside, AT-, DM- ja NMEA-pordid on USB kaudu); UART-i mPCIe pistikul **pole** | **UART** mPCIe viikudel 17/19; USB D+/D− (36/38) **pole ESP32-ga ühendatud** |
| Viigud 17/19 | reserveeritud / ühendamata | ESP32 UART (GPIO46/45) |
| Toide | mPCIe standard **3,3 V** (umbes 3,0–3,6 V) | **4,2 V** (DVDD4V2, SIM7600 jaoks) |
| Sisselülitus | käivitub toite saamisel (`W_DISABLE#`, viik 20), PWRKEY-d pole | GPIO48 → viik 6 (standardis +1,5 V) |

1. **Võta MC7304 T-SIMCAM-i pesast välja.** 4,2 V on üle kaardi lubatud
   toitepinge – kaart võib kuumeneda ja rikki minna (kui see on juba pesas olnud,
   kontrolli enne muud kasutust, kas see töötab nt USB-adapteriga arvutis).
2. ⚙ → Mobiilivõrk → võta **„LTE modem sees"** maha (siis ei saadeta pessa
   PWRKEY impulsse ega AT-käske).
3. Püsivara muudatusega seda lahendada ei saa: ESP32 ja kaardi vahel puudub
   ühine liides (kaardil USB, pesal UART), lisaks on toide vale.

MC7304 kasutamiseks on vaja seadet, millel on mPCIe pesa koos **USB host**
liidese ja 3,3 V toitega (nt ruuter või USB–mPCIe adapter arvutis); ESP32-S3-ga
otse ühendades tuleks ehitada eraldi plaat (3,3 V regulaator ≥ 2 A + USB OTG
host + QMI/MBIM draiver), mida see projekt ei toeta.

**Sobib:** LilyGO **T-PCIe SIM7600E-H** (Euroopa sagedused B1/B3/B7/B8/B20),
`MODEM_TYPE_SIM7600`, CMUX, kuni 3 Mbit/s UART. `MODEM_TYPE_GENERIC` sobib muu
UART-iga 3GPP modemiga, mis talub 4,2 V toidet.

Allikad: [MikroTik R11e-LTE](https://mikrotik.com/product/r11e_lte),
Sierra Wireless *AirPrime MC7304 Product Technical Specification*,
[OpenWrt #11400](https://github.com/openwrt/openwrt/issues/11400),
[T-SIMCAM skeem](https://github.com/Xinyuan-LilyGO/LilyGo-Camera-Series/tree/master/schematic),
[LilyGO T-SIMCAM wiki](https://wiki.lilygo.cc/products/t-sim-series/t-simcam/).

## Veaotsing

| Sümptom | Põhjus / lahendus |
|---|---|
| `PSRAM puudub!` | `board_build.arduino.memory_type = qio_opi` peab olema `platformio.ini`-s |
| `esp_camera_init ebaõnnestus: 0x105` | kaamera kaabel lahti või vale suunaga |
| Hotspoti leht ei avane ise | ava `http://4.3.2.1/`; lülita telefonis välja „Privaatne DNS"/VPN |
| `Modem ei vasta UART-il` | vale kaart (USB-ainult, nt R11e-LTE või MC7304 – eemalda!) või modem pole pesas korralikult |
| `APN on seadistamata` / `PIN on seadistamata` | ⚙ → Mobiilivõrk |
| `SIM PIN vale!` | paranda PIN seadetes (sama valet PIN-i enam ei proovita) |
| `SIM on PUK lukus!` | ava SIM telefonis PUK-koodiga |
| LTE IP pole see, mida ootasid | APN vale või staatilise IP teenus pole SIM-ile aktiveeritud |
| FOTA: „GitHubiga ei saanud ühendust" | seadmel pole internetti (ainult hotspot ei piisa) |
| Seade taaskäivitub ise | ⚙ → 📄 Logi → „Eelmine käivitus": põhjus ja viimased read enne taaskäivitust |
| Heli ei kõla / helitase ~ −90 dBFS | mikrofon seadetes väljas või logis „Mikrofon ei anna signaali" |
| Heli moonutab | vähenda võimendust (tipp ei tohi olla punane) |
| Heli asemel ainult sahin | vaata logist `MIC` „Proov …" ridu; mõlemas kanalis ~−5 dBFS = mikrofon ei tööta selle seadistusega |
| Brownout LTE ühendumisel | modem tarbib kuni 2 A tippe – kasuta korralikku 5 V toidet |

## Projekti struktuur

```
SimCam/
├── platformio.ini            PlatformIO (pioarduino, Arduino-ESP32 3.x, 16 MB, OPI PSRAM)
├── tools/version.py          versioon git sildist → SIMCAM_VERSION
├── tools/flasher/            Windowsi paigaldaja (simcam-flasher-*.exe allikas)
├── web-flasher/              brauseri-paigaldusleht (GitHub Pages, ESP Web Tools)
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
    ├── battery.*             aku pinge (GPIO3), täituvus, olek, ajalugu
    ├── audio.*               mikrofon (PDM), heli ringpuhver, G.711 kodeerija
    ├── settings.*            NVS seaded
    ├── auth.*                parool, sessiooniküpsis, HTTP Basic
    └── log.*                 logi: USB Serial + mälupuhver (/log) + eelmise käivituse logi (RTC)
```

