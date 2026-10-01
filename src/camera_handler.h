// =============================================================================
//  Camera_Manager – kaamera draiver, autofookus ja kaadrijagur
// =============================================================================
//  Üks hõivetask võtab kaadreid kaamerast ja kopeerib viimase JPEG-i jagatud
//  PSRAM puhvrisse. Tarbijad (RTSP, MJPEG, /capture) kopeerivad selle endale.
//  Nii ei hoia aeglane mobiilivõrgu klient kunagi kaamera kaadripuhvrit kinni
//  ja iga klient saab alati kõige värskema kaadri (madal viivitus – vanad
//  kaadrid jäetakse vahele, neid ei panda järjekorda).
// =============================================================================
#pragma once

#include <Arduino.h>

namespace Camera {

// Tarbija isiklik kaadrikoopia. Puhver on PSRAM-is ja kasvab vajadusel.
struct Frame {
    uint8_t *buf = nullptr;
    size_t   len = 0;
    size_t   cap = 0;
    uint32_t seq = 0;        // kaadri järjekorranumber (0 = pole veel)
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t timestampMs = 0;
};

bool begin();                 // initsialiseeri kaamera + AF + hõivetask
bool ready();

// Oota kaadrit, mille seq != lastSeq, ja kopeeri see dst-sse.
// Tagastab false, kui timeout või kaamera pole valmis.
bool waitFrame(Frame &dst, uint32_t lastSeq, uint32_t timeoutMs);
void freeFrame(Frame &f);

// Autofookus
bool afSupported();
bool refocus();               // käivita ühekordne fookus (nupp "Refocus")
const char *afStatus();       // "focused", "focusing", "idle", "n/a", ...

// Pildiseaded (sama nimetus kui esp32-camera CameraWebServer näites):
// framesize, quality, brightness, contrast, saturation, sharpness, aec, aec2,
// ae_level, aec_value, agc, agc_gain, gainceiling, awb, awb_gain, wb_mode,
// hmirror, vflip, special_effect, ir (IR-filtri viik, V1.3)
bool control(const char *var, int val);
int  readReg(int reg);            // SCCB registri lugemine (diagnostika), -1 = viga
String settingsJson();            // hetkeseaded JSON-ina

// Pööramine 0 või 180 kraadi (sensor; salvestatakse NVS-i)
bool setRotation(int deg);
int  rotation();

// Info veebiliidesele
const char *sensorName();
const char *resolutionName();

// Resolutsioon (esp32-camera framesize_t). Lubatud: QVGA … FHD, sensori piires.
bool setFramesize(int fs, bool save = true);
int  framesize();
String framesizesJson();          // [{"v":11,"name":"SVGA","w":800,"h":600},…] – sensori toetatud
float fps();                  // tegelik hõive FPS
size_t lastFrameBytes();
void addConsumer();           // loendab aktiivseid vaatajaid
void removeConsumer();
int consumers();

}  // namespace Camera
