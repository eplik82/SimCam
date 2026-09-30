// =============================================================================
//  FOTA – teostus
// =============================================================================
#include "ota.h"
#include "version.h"
#include "log.h"

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "settings.h"
#include <Preferences.h>

static const char *TAG = "OTA";

#define OTA_CHECK_HOURS      6
#define OTA_FIRST_CHECK_MS   60000UL
#define OTA_VALIDATE_MS      60000UL
#define OTA_JSON_MAX         (192 * 1024)

// Arduino core: ära märgi rakendust kohe kehtivaks – teeme seda ise pärast
// tervisekontrolli (vt validateTask). Nii toimib bootloaderi tagasipööramine.
extern "C" bool verifyRollbackLater() { return true; }

namespace Ota {

enum class St : uint8_t { Idle, Checking, UpToDate, Available, Updating, Done, Error };

static SemaphoreHandle_t s_mtx;
static TaskHandle_t      s_task;
static volatile St       s_st = St::Idle;
static volatile int      s_progress = 0;
static char              s_latest[24] = "";
static char              s_relName[64] = "";
static char              s_url[256] = "";
static char              s_err[96] = "";
static uint32_t          s_size = 0;
static uint32_t          s_lastCheck = 0;       // millis() viimasest kontrollist
static bool              s_pendingVerify = false;
static volatile uint32_t s_checkAt = 0;          // hilinenud kontrolli aeg (millis), 0 = pole
static char              s_skip[24] = "";      // versioon, mis tagasi pöörati – auto ei proovi uuesti

// Enne uuendamist salvestatakse proovitav versioon. Käivitusel: kui töötab
// see sama versioon → õnnestus; kui töötab midagi muud → uus pilt ei
// käivitunud (bootloader pööras tagasi) → jäta see versioon automaatikast välja.
static void rememberTry(const char *v) {
    Preferences p;
    if (p.begin("simcam", false)) { p.putString("ota_try", v); p.end(); }
}

static void resolveTry() {
    Preferences p;
    if (!p.begin("simcam", false)) return;
    String tried = p.getString("ota_try", "");
    String skip = p.getString("ota_skip", "");
    if (tried.length()) {
        if (tried == SIMCAM_VERSION) {
            skip = "";                                // uus versioon käivitus
        } else {
            skip = tried;                             // tagasipööramine
            LOGW(TAG, "Uuendus %s ei käivitunud (tagasi pööratud) – automaatne uuendus jätab selle vahele", tried.c_str());
        }
        p.remove("ota_try");
        p.putString("ota_skip", skip);
    }
    strlcpy(s_skip, skip.c_str(), sizeof(s_skip));
    p.end();
}

enum : uint32_t { CMD_CHECK = 1, CMD_UPDATE = 2 };

static const char *stName(St s) {
    switch (s) {
        case St::Idle: return "idle";
        case St::Checking: return "checking";
        case St::UpToDate: return "uptodate";
        case St::Available: return "available";
        case St::Updating: return "updating";
        case St::Done: return "done";
        case St::Error: return "error";
    }
    return "?";
}

static void setErr(const char *e) {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    strlcpy(s_err, e, sizeof(s_err));
    s_st = St::Error;
    xSemaphoreGive(s_mtx);
    LOGE(TAG, "%s", e);
}

// "v1.2.3" / "1.2.3-dev" → 1002003; tundmatu → 0
static uint32_t verNum(const char *v) {
    if (*v == 'v' || *v == 'V') v++;
    int a = 0, b = 0, c = 0;
    sscanf(v, "%d.%d.%d", &a, &b, &c);
    return (uint32_t)a * 1000000 + b * 1000 + c;
}

// -----------------------------------------------------------------------------
//  Kontroll: GitHub API → viimane release
// -----------------------------------------------------------------------------
static void doCheck() {
    s_st = St::Checking;
    s_err[0] = 0;
    s_lastCheck = millis();

    esp_http_client_config_t c = {};
    c.url = "https://api.github.com/repos/" SIMCAM_GITHUB_REPO "/releases/latest";
    c.crt_bundle_attach = esp_crt_bundle_attach;
    c.timeout_ms = 15000;
    c.buffer_size = 4096;
    c.buffer_size_tx = 1024;
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) { setErr("HTTP klienti ei saanud luua"); return; }
    esp_http_client_set_header(h, "User-Agent", "SimCam-ESP32/" SIMCAM_VERSION);
    esp_http_client_set_header(h, "Accept", "application/vnd.github+json");

    char *buf = nullptr;
    int len = 0, status = 0;
    esp_err_t err = esp_http_client_open(h, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(h);
        status = esp_http_client_get_status_code(h);
        buf = (char *)heap_caps_malloc(OTA_JSON_MAX, MALLOC_CAP_SPIRAM);
        if (buf) {
            int r;
            while (len < OTA_JSON_MAX - 1 && (r = esp_http_client_read(h, buf + len, OTA_JSON_MAX - 1 - len)) > 0)
                len += r;
            buf[len] = 0;
        }
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);

    if (err != ESP_OK) {
        free(buf);
        char e[96];
        snprintf(e, sizeof(e), "GitHubiga ei saanud ühendust (%s) – kas internet on olemas?", esp_err_to_name(err));
        setErr(e);
        return;
    }
    if (status == 404) { free(buf); setErr("GitHubis pole veel ühtegi release'i"); return; }
    if (status != 200 || !buf) {
        free(buf);
        char e[64];
        snprintf(e, sizeof(e), "GitHub API vastas HTTP %d", status);
        setErr(e);
        return;
    }

    cJSON *j = cJSON_Parse(buf);
    free(buf);
    if (!j) { setErr("Release'i JSON-i ei saanud lugeda"); return; }
    const cJSON *tag = cJSON_GetObjectItem(j, "tag_name");
    const cJSON *name = cJSON_GetObjectItem(j, "name");
    const cJSON *assets = cJSON_GetObjectItem(j, "assets");
    const char *url = nullptr;
    uint32_t size = 0;
    const cJSON *a;
    cJSON_ArrayForEach(a, assets) {
        const cJSON *an = cJSON_GetObjectItem(a, "name");
        if (cJSON_IsString(an) && !strcmp(an->valuestring, SIMCAM_FW_ASSET)) {
            const cJSON *u = cJSON_GetObjectItem(a, "browser_download_url");
            const cJSON *sz = cJSON_GetObjectItem(a, "size");
            if (cJSON_IsString(u)) url = u->valuestring;
            if (cJSON_IsNumber(sz)) size = (uint32_t)sz->valuedouble;
        }
    }
    if (!cJSON_IsString(tag)) { cJSON_Delete(j); setErr("Release'il puudub silt (tag)"); return; }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    strlcpy(s_latest, tag->valuestring[0] == 'v' ? tag->valuestring + 1 : tag->valuestring, sizeof(s_latest));
    strlcpy(s_relName, cJSON_IsString(name) ? name->valuestring : "", sizeof(s_relName));
    for (char *q = s_relName; *q; q++) if (*q == '"' || *q == '\\' || (uint8_t)*q < 0x20) *q = ' ';
    strlcpy(s_url, url ? url : "", sizeof(s_url));
    s_size = size;
    xSemaphoreGive(s_mtx);
    cJSON_Delete(j);

    const bool newer = verNum(s_latest) > verNum(SIMCAM_VERSION);
    if (newer && !s_url[0]) {
        setErr("Release'il puudub fail " SIMCAM_FW_ASSET);
        return;
    }
    s_st = newer ? St::Available : St::UpToDate;
    LOGI(TAG, "Praegune %s, viimane %s → %s", SIMCAM_VERSION, s_latest,
         newer ? "UUENDUS SAADAVAL" : "ajakohane");
}

static void doUpdate();

// Automaatne paigaldus pärast kontrolli (kui seadetes lubatud)
static void maybeAutoUpdate() {
    if (s_st != St::Available || !Settings::get().autoUpdate) return;
    if (s_skip[0] && !strcmp(s_skip, s_latest)) {
        LOGW(TAG, "Automaatne uuendus: %s jäeti vahele (varem tagasi pööratud)", s_latest);
        return;
    }
    LOGI(TAG, "Automaatne uuendus: paigaldan %s", s_latest);
    doUpdate();
}

// -----------------------------------------------------------------------------
//  Uuendamine: laadi alla ja kirjuta teise OTA partitsiooni
// -----------------------------------------------------------------------------
static void doUpdate() {
    if (!s_url[0]) { setErr("Uuenduse aadress puudub – kontrolli enne uuendusi"); return; }
    s_st = St::Updating;
    s_progress = 0;
    s_err[0] = 0;
    LOGI(TAG, "Laen alla %s (%lu B)", s_url, (unsigned long)s_size);
    rememberTry(s_latest);

    esp_http_client_config_t hc = {};
    hc.url = s_url;
    hc.crt_bundle_attach = esp_crt_bundle_attach;
    hc.timeout_ms = 30000;
    hc.buffer_size = 8192;          // GitHubi ümbersuunamise URL on pikk
    hc.buffer_size_tx = 4096;
    hc.keep_alive_enable = true;
    hc.max_redirection_count = 5;
    hc.user_agent = "SimCam-ESP32/" SIMCAM_VERSION;

    esp_https_ota_config_t oc = {};
    oc.http_config = &hc;

    esp_https_ota_handle_t oh = nullptr;
    esp_err_t err = esp_https_ota_begin(&oc, &oh);
    if (err != ESP_OK) {
        char e[96];
        snprintf(e, sizeof(e), "Allalaadimist ei saanud alustada (%s)", esp_err_to_name(err));
        setErr(e);
        return;
    }

    // Kontrolli pildi päist enne kirjutamist
    esp_app_desc_t desc;
    if (esp_https_ota_get_img_desc(oh, &desc) == ESP_OK)
        LOGI(TAG, "Uus pilt: versioon '%s', ehitatud %s %s", desc.version, desc.date, desc.time);

    const int total = esp_https_ota_get_image_size(oh);
    uint32_t lastLog = 0;
    while ((err = esp_https_ota_perform(oh)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int got = esp_https_ota_get_image_len_read(oh);
        if (total > 0) s_progress = (int)((int64_t)got * 100 / total);
        if (millis() - lastLog > 3000) {
            lastLog = millis();
            LOGI(TAG, "Uuendus %d%% (%d / %d B)", s_progress, got, total);
        }
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(oh)) {
        esp_https_ota_abort(oh);
        char e[96];
        snprintf(e, sizeof(e), "Allalaadimine katkes (%s)", esp_err_to_name(err));
        setErr(e);
        return;
    }
    err = esp_https_ota_finish(oh);   // kontrollib pildi terviklikkust + määrab boot-partitsiooni
    if (err != ESP_OK) {
        char e[96];
        snprintf(e, sizeof(e), "Püsivara kontroll ebaõnnestus (%s)", esp_err_to_name(err));
        setErr(e);
        return;
    }
    s_progress = 100;
    s_st = St::Done;
    LOGI(TAG, "Uuendus %s paigaldatud – taaskäivitan", s_latest);
    vTaskDelay(pdMS_TO_TICKS(2500));   // lase veebiliidesel "valmis" näidata
    ESP.restart();
}

// -----------------------------------------------------------------------------
static void otaTask(void *) {
    uint32_t nextCheck = millis() + OTA_FIRST_CHECK_MS;
    uint32_t validateAt = millis() + OTA_VALIDATE_MS;
    for (;;) {
        uint32_t cmd = 0;
        xTaskNotifyWait(0, UINT32_MAX, &cmd, pdMS_TO_TICKS(5000));

        // Tervisekontroll: 60 s tõrgeteta tööd → uus püsivara kehtib
        if (s_pendingVerify && (int32_t)(millis() - validateAt) >= 0) {
            s_pendingVerify = false;
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
                LOGI(TAG, "Uus püsivara %s kinnitatud (tagasipööramine tühistatud)", SIMCAM_VERSION);
        }

        if (s_checkAt && (int32_t)(millis() - s_checkAt) >= 0) { s_checkAt = 0; cmd |= CMD_CHECK; }
        if (cmd & CMD_UPDATE) { doUpdate(); continue; }
        if ((cmd & CMD_CHECK) || (int32_t)(millis() - nextCheck) >= 0) {
            if (s_st != St::Updating) { doCheck(); maybeAutoUpdate(); }
            nextCheck = millis() + OTA_CHECK_HOURS * 3600UL * 1000UL;
        }
    }
}

void begin() {
    s_mtx = xSemaphoreCreateMutex();
    resolveTry();
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK) {
        s_pendingVerify = (st == ESP_OTA_IMG_PENDING_VERIFY);
        if (s_pendingVerify)
            LOGW(TAG, "Uus püsivara %s ootab kinnitust (%lu s)", SIMCAM_VERSION, OTA_VALIDATE_MS / 1000);
        else if (st != ESP_OTA_IMG_VALID)
            esp_ota_mark_app_valid_cancel_rollback();     // nt USB kaudu laaditud
    }
    LOGI(TAG, "Püsivara %s, partitsioon %s, uuenduste allikas github.com/%s",
         SIMCAM_VERSION, run ? run->label : "?", SIMCAM_GITHUB_REPO);
    xTaskCreatePinnedToCore(otaTask, "ota", 10240, nullptr, 2, &s_task, 0);
}

void checkNow(uint32_t delayMs) {
    if (!s_task) return;
    if (delayMs) { s_checkAt = millis() + delayMs; if (!s_checkAt) s_checkAt = 1; return; }
    xTaskNotify(s_task, CMD_CHECK, eSetBits);
}

bool startUpdate() {
    if (!s_task || s_st == St::Updating || !s_url[0]) return false;
    if (verNum(s_latest) <= verNum(SIMCAM_VERSION)) return false;
    s_st = St::Updating;
    s_progress = 0;
    xTaskNotify(s_task, CMD_UPDATE, eSetBits);
    return true;
}

String statusJson() {
    char b[480];
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    uint32_t ago = s_lastCheck ? (millis() - s_lastCheck) / 1000 : 0;
    snprintf(b, sizeof(b),
             "{\"current\":\"%s\",\"latest\":\"%s\",\"name\":\"%s\",\"state\":\"%s\","
             "\"progress\":%d,\"size\":%lu,\"checked_ago\":%lu,\"error\":\"%s\",\"repo\":\"%s\","
             "\"auto\":%s,\"skip\":\"%s\",\"interval_h\":%d}",
             SIMCAM_VERSION, s_latest, s_relName, stName(s_st), s_progress, (unsigned long)s_size,
             (unsigned long)(s_lastCheck ? ago : 0), s_err, SIMCAM_GITHUB_REPO,
             Settings::get().autoUpdate ? "true" : "false", s_skip, OTA_CHECK_HOURS);
    xSemaphoreGive(s_mtx);
    return String(b);
}

}  // namespace Ota
