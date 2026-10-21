// =============================================================================
//  HttpsCert – teostus
// =============================================================================
#include "https_cert.h"
#include "log.h"
#include <Preferences.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "mbedtls/platform.h"
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha256.h"

static const char *TAG = "TLS";
static const char *NS  = "simcam_tls";

namespace HttpsCert {

static String s_crt, s_key;
static char   s_fp[96] = "";

// --- mbedTLS mälu: PSRAM eelistatud ------------------------------------------
// Raamistik on ehitatud MBEDTLS_INTERNAL_MEM_ALLOC-iga: iga TLS sessioon võtaks
// ~40 kB sisemist RAM-i (vaba on ~100 kB). heap_caps_free vabastab mõlemat liiki.
static void *tlsCalloc(size_t n, size_t sz) {
    void *p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void tlsFree(void *p) { heap_caps_free(p); }

void usePsramForTls() { mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree); }

static int rng(void *, unsigned char *buf, size_t len) {
    esp_fill_random(buf, len);          // riistvaraline RNG (RF sees → tõeline juhuslikkus)
    return 0;
}

static bool generate() {
    const uint32_t t0 = millis();
    mbedtls_pk_context pk;
    mbedtls_x509write_cert w;
    mbedtls_pk_init(&pk);
    mbedtls_x509write_crt_init(&w);
    const size_t BUF = 2048;
    unsigned char *buf = (unsigned char *)malloc(BUF);
    bool ok = false;
    int r = -1;
    do {
        if (!buf) break;
        if ((r = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)))) break;
        if ((r = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), rng, nullptr))) break;

        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char name[64];
        snprintf(name, sizeof(name), "CN=SimCam-%02X%02X%02X,O=SimCam", mac[3], mac[4], mac[5]);
        unsigned char serial[16];
        esp_fill_random(serial, sizeof(serial));
        serial[0] = (serial[0] & 0x7F) | 0x01;   // positiivne, mitte null

        mbedtls_x509write_crt_set_version(&w, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&w, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&w, &pk);
        mbedtls_x509write_crt_set_issuer_key(&w, &pk);
        if ((r = mbedtls_x509write_crt_set_subject_name(&w, name))) break;
        if ((r = mbedtls_x509write_crt_set_issuer_name(&w, name))) break;
        if ((r = mbedtls_x509write_crt_set_serial_raw(&w, serial, sizeof(serial)))) break;
        // Seadmel pole kella enne võrku – kehtivus fikseeritud vahemikuna
        if ((r = mbedtls_x509write_crt_set_validity(&w, "20250101000000", "20491231235959"))) break;
        if ((r = mbedtls_x509write_crt_set_basic_constraints(&w, 0, -1))) break;
        if ((r = mbedtls_x509write_crt_pem(&w, buf, BUF, rng, nullptr))) break;
        s_crt = (const char *)buf;
        if ((r = mbedtls_pk_write_key_pem(&pk, buf, BUF))) break;
        s_key = (const char *)buf;
        ok = true;
    } while (false);
    if (buf) { memset(buf, 0, BUF); free(buf); }
    mbedtls_x509write_crt_free(&w);
    mbedtls_pk_free(&pk);
    if (!ok) { LOGE(TAG, "Sertifikaadi loomine ebaõnnestus (-0x%04X)", (unsigned)-r); return false; }

    Preferences p;
    if (p.begin(NS, false)) { p.putString("crt", s_crt); p.putString("key", s_key); p.end(); }
    LOGI(TAG, "Uus isetehtud HTTPS sertifikaat loodud (%lu ms)", (unsigned long)(millis() - t0));
    return true;
}

static void computeFingerprint() {
    s_fp[0] = 0;
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse(&crt, (const unsigned char *)s_crt.c_str(), s_crt.length() + 1) == 0) {
        unsigned char h[32];
        mbedtls_sha256(crt.raw.p, crt.raw.len, h, 0);
        char *o = s_fp;
        for (int i = 0; i < 32; i++) o += sprintf(o, i ? ":%02X" : "%02X", h[i]);
    }
    mbedtls_x509_crt_free(&crt);
}

bool ensure() {
    Preferences p;
    if (p.begin(NS, true)) {
        s_crt = p.getString("crt", "");
        s_key = p.getString("key", "");
        p.end();
    }
    if (!s_crt.length() || !s_key.length()) {
        if (!generate()) return false;
    }
    computeFingerprint();
    if (!s_fp[0]) {                      // rikutud NVS kirje → loo uus
        LOGW(TAG, "Salvestatud sertifikaat vigane – loon uue");
        if (!generate()) return false;
        computeFingerprint();
    }
    LOGI(TAG, "HTTPS sertifikaadi SHA-256: %.23s…", s_fp);
    return true;
}

const char *certPem() { return s_crt.c_str(); }
const char *keyPem() { return s_key.c_str(); }
const char *fingerprint() { return s_fp; }

void erase() {
    Preferences p;
    if (p.begin(NS, false)) { p.clear(); p.end(); }
    LOGW(TAG, "HTTPS sertifikaat kustutatud – uus luuakse taaskäivitusel");
}

}  // namespace HttpsCert
