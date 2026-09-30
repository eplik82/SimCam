// =============================================================================
//  Juurdepääsu kaitse
//   * Veebiliides: sisselogimise leht → küpsis "simcam" (SHA-256 soolast +
//     paroolist). Kehtib ka pärast taaskäivitust; parooli vahetus tühistab.
//   * RTSP ja skriptid: HTTP Basic (kasutaja "admin", sama parool).
// =============================================================================
#pragma once
#include <Arduino.h>

namespace Auth {

bool enabled();                                // kas parool on määratud
bool checkPassword(const char *pass);          // ajakindel võrdlus
bool check(const char *authorizationHeader);   // HTTP Basic päis
bool checkCookie(const char *cookieHeader);    // "Cookie:" päise väärtus
String cookieValue();                          // kehtiv sessioonitunnus
bool setPassword(const char *newPass);         // salvestab NVS-i (min 4 märki)

}  // namespace Auth
