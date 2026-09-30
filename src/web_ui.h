// =============================================================================
//  Veebiliides (HTTP port 80) – ESP-IDF esp_http_server põhjal
// =============================================================================
//   GET  /             – veebiliides (HTML PROGMEM-ist)
//   GET  /stream       – MJPEG reaalajavoog (multipart/x-mixed-replace)
//   GET  /capture      – üks JPEG kaader
//   GET  /api/status   – JSON: modem, kaamera, süsteem
//   POST /api/focus    – käivita autofookus
//   POST /api/reboot   – taaskäivita ESP32
//
//  MJPEG voog kasutab asünkroonset käsitlejat (oma task), seega ei blokeeri
//  pikaajaline voog serveri ainsat töötlustaski – API ja leht jäävad kiireks.
// =============================================================================
#pragma once

namespace WebUI {

bool begin();
int streamClients();

}  // namespace WebUI
