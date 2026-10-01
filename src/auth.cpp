// =============================================================================
//  Juurdepääsu kaitse – teostus
// =============================================================================
#include "auth.h"
#include "config.h"
#include "settings.h"
#include "log.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

static const char *TAG = "AUTH";

namespace Auth {

// Võrdle konstantse ajaga (ei leki ajastuse kaudu, mitu märki klappis)
static bool ctEqual(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    uint8_t diff = la != lb;
    for (size_t i = 0; i < la && i < lb; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

bool enabled() { return Settings::get().webPass[0] != 0; }

bool checkPassword(const char *pass) {
    if (!enabled()) return true;
    return pass && ctEqual(pass, Settings::get().webPass);
}

bool check(const char *hdr) {
    if (!enabled()) return true;
    if (!hdr) return false;
    while (*hdr == ' ') hdr++;
    if (strncasecmp(hdr, "Basic ", 6) != 0) return false;
    hdr += 6;
    while (*hdr == ' ') hdr++;
    char b64[128];
    size_t n = 0;
    while (hdr[n] && hdr[n] != '\r' && hdr[n] != '\n' && hdr[n] != ' ' && n < sizeof(b64) - 1) { b64[n] = hdr[n]; n++; }
    b64[n] = 0;
    unsigned char dec[100];
    size_t olen = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &olen, (const unsigned char *)b64, n) != 0) return false;
    dec[olen] = 0;
    char *colon = strchr((char *)dec, ':');
    if (!colon) return false;
    *colon = 0;
    // Kasutajanimi ei ole oluline – ainult parool (VLC/ffmpeg saadavad "admin")
    return checkPassword(colon + 1);
}

// Basic päise kasutajanimi logimiseks (mitteprinditavad märgid → '?', piiratud pikkus)
void basicUser(const char *hdr, char *out, size_t n) {
    out[0] = 0;
    while (*hdr == ' ') hdr++;
    if (strncasecmp(hdr, "Basic ", 6) != 0) { strlcpy(out, "(mitte Basic)", n); return; }
    hdr += 6;
    while (*hdr == ' ') hdr++;
    unsigned char dec[100];
    size_t olen = 0, len = strcspn(hdr, " \r\n");
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &olen, (const unsigned char *)hdr, len) != 0) { strlcpy(out, "(vigane)", n); return; }
    dec[olen] = 0;
    char *colon = strchr((char *)dec, ':');
    if (colon) *colon = 0;
    size_t j = 0;
    for (size_t i = 0; dec[i] && j + 1 < n && j < 32; i++) out[j++] = (dec[i] >= 0x20 && dec[i] < 0x7F) ? dec[i] : '?';
    out[j] = 0;
}

String cookieValue() {
    Settings::Data d = Settings::get();
    uint8_t h[32];
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    mbedtls_sha256_update(&c, (const uint8_t *)d.authSalt, strlen(d.authSalt));
    mbedtls_sha256_update(&c, (const uint8_t *)":", 1);
    mbedtls_sha256_update(&c, (const uint8_t *)d.webPass, strlen(d.webPass));
    mbedtls_sha256_finish(&c, h);
    mbedtls_sha256_free(&c);
    char hex[41];
    for (int i = 0; i < 20; i++) sprintf(hex + i * 2, "%02x", h[i]);
    return String(hex);
}

bool checkCookie(const char *cookies) {
    if (!enabled()) return true;
    if (!cookies) return false;
    const char *p = strstr(cookies, "simcam=");
    while (p && p != cookies && p[-1] != ' ' && p[-1] != ';') p = strstr(p + 1, "simcam=");
    if (!p) return false;
    p += 7;
    char v[48];
    size_t n = 0;
    while (p[n] && p[n] != ';' && p[n] != ' ' && n < sizeof(v) - 1) { v[n] = p[n]; n++; }
    v[n] = 0;
    return ctEqual(v, cookieValue().c_str());
}

bool setPassword(const char *np) {
    if (!np || strlen(np) < 4 || strlen(np) > 64) return false;
    Settings::Data d = Settings::get();
    strlcpy(d.webPass, np, sizeof(d.webPass));
    bool ok = Settings::save(d);
    LOGI(TAG, "Parool %s", ok ? "muudetud" : "muutmine ebaõnnestus");
    return ok;
}

}  // namespace Auth
