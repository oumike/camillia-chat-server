#include "ota_update.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <string.h>
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "ota_signing_pubkey.h"
#include "ota_version.h"
#include "status_display.h"   // CS_VERSION

#ifndef CS_PIO_ENV
#define CS_PIO_ENV ""
#endif

namespace {

// Hard-coded so the update source can't be repointed from the web config. Plain
// HTTP (as in camillia-mt): authenticity comes from the signature, not transport.
const char kBaseUrl[] = "http://ota.camillia.sumat.org/cs-firmware";
const char kUserAgent[] = "camillia-chat-server-ota";
constexpr uint32_t kHttpTimeoutMs = 12000;
constexpr uint32_t kStallMs = 15000;
constexpr uint32_t kFirstCheckDelayMs = 20000;           // after WiFi joins
constexpr uint32_t kRecheckMs = 24UL * 60 * 60 * 1000;   // daily
constexpr uint32_t kRetryMs = 60UL * 60 * 1000;          // after a failed check
constexpr size_t   kReleaseJsonMax = 16 * 1024;          // tag_name precedes the assets

enum State : uint8_t { ST_IDLE, ST_CHECKING, ST_DOWNLOADING, ST_VERIFYING, ST_INSTALLED, ST_ERROR };
const char *const kStateNames[] = {"idle", "checking", "downloading", "verifying", "installed", "error"};

struct Status {
    State    state;
    bool     checked, updateAvailable;
    char     latest[48];
    char     error[96];
    uint32_t checkedAtMs, written, total;
};

enum Job : uint8_t { JOB_NONE, JOB_CHECK, JOB_INSTALL };

const Settings   *s_cfg = nullptr;
Status            s_st = {};
portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t      s_task = nullptr;
volatile Job      s_job = JOB_NONE;
volatile bool     s_busy = false;
uint32_t          s_wifiUpAtMs = 0;
uint32_t          s_nextCheckAtMs = 0;
bool              s_autoTried = false;   // one unattended attempt per offered tag
volatile bool     s_checkFailed = false;

void setState(State st, const char *err = nullptr) {
    portENTER_CRITICAL(&s_mux);
    s_st.state = st;
    if (st == ST_CHECKING || st == ST_DOWNLOADING) s_st.error[0] = 0;
    if (err) snprintf(s_st.error, sizeof s_st.error, "%s", err);
    portEXIT_CRITICAL(&s_mux);
}

void setProgress(uint32_t written, uint32_t total) {
    portENTER_CRITICAL(&s_mux);
    s_st.written = written;
    s_st.total = total;
    portEXIT_CRITICAL(&s_mux);
}

Status snapshot() {
    portENTER_CRITICAL(&s_mux);
    Status s = s_st;
    portEXIT_CRITICAL(&s_mux);
    return s;
}

// Same layout check as camillia-mt: two equal OTA slots, the first at 0x10000,
// no third, and we are running from one of them. Fails closed.
bool layoutSupportsUpdate() {
    const esp_partition_t *s0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t *s1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    const esp_partition_t *s2 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_2, nullptr);
    if (!s0 || !s1 || s2 || s0->size != s1->size || s0->address != 0x10000) return false;
    const esp_partition_t *run = esp_ota_get_running_partition();
    return run && (run->address == s0->address || run->address == s1->address);
}

bool supported() { return CS_PIO_ENV[0] && layoutSupportsUpdate(); }

void beginHttp(HTTPClient &http, WiFiClient &client, const char *url, uint32_t timeoutMs) {
    http.begin(client, url);
    http.setTimeout((uint16_t)timeoutMs);
    http.addHeader("User-Agent", kUserAgent);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
}

// Reads up to cap bytes of the response body; stops at cap, the end, or a stall.
size_t readBody(HTTPClient &http, uint8_t *buf, size_t cap) {
    const int total = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    size_t n = 0;
    uint32_t lastMs = millis();
    while (stream && n < cap && (total < 0 || n < (size_t)total)) {
        size_t avail = stream->available();
        if (!avail) {
            if (!http.connected() || millis() - lastMs > 5000) break;
            delay(5);
            continue;
        }
        if (avail > cap - n) avail = cap - n;
        int r = stream->readBytes(buf + n, avail);
        if (r > 0) { n += (size_t)r; lastMs = millis(); }
    }
    return n;
}

bool fetchLatestTag(char *tag, size_t cap, char *err, size_t errCap) {
    char url[96];
    snprintf(url, sizeof url, "%s/latest", kBaseUrl);
    WiFiClient client;
    HTTPClient http;
    beginHttp(http, client, url, kHttpTimeoutMs);
    http.addHeader("Accept", "application/vnd.github+json");
    http.useHTTP10(true);   // never chunked: the body is read raw below
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        snprintf(err, errCap, code > 0 ? "Release check: HTTP %d" : "Release check: network error (%d)", code);
        http.end();
        return false;
    }
    char *buf = (char *)ps_malloc(kReleaseJsonMax + 1);
    if (!buf) buf = (char *)malloc(kReleaseJsonMax + 1);
    if (!buf) { http.end(); snprintf(err, errCap, "Out of memory"); return false; }
    size_t n = readBody(http, (uint8_t *)buf, kReleaseJsonMax);
    http.end();
    buf[n] = 0;
    const bool ok = otaFindTagName(buf, tag, cap);
    free(buf);
    if (!ok) snprintf(err, errCap, "Release check: no tag_name in reply");
    return ok;
}

bool verifySignature(const uint8_t hash[32], const uint8_t *sig, size_t sigLen) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)OTA_SIGNING_PUBKEY_PEM,
                                         strlen(OTA_SIGNING_PUBKEY_PEM) + 1);
    if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen);
    mbedtls_pk_free(&pk);
    if (rc) Serial.printf("[cs] ota: signature check failed -0x%04x\n", (unsigned)-rc);
    return rc == 0;
}

void doCheck() {
    setState(ST_CHECKING);
    char tag[48], err[96];
    if (!fetchLatestTag(tag, sizeof tag, err, sizeof err)) {
        Serial.printf("[cs] ota: %s\n", err);
        setState(ST_ERROR, err);
        s_checkFailed = true;
        return;
    }
    const bool avail = otaUpdateAvailable(CS_VERSION, tag);
    portENTER_CRITICAL(&s_mux);
    if (strcmp(s_st.latest, tag) != 0) s_autoTried = false;
    snprintf(s_st.latest, sizeof s_st.latest, "%s", tag);
    s_st.checked = true;
    s_st.updateAvailable = avail;
    s_st.checkedAtMs = millis();
    portEXIT_CRITICAL(&s_mux);
    Serial.printf("[cs] ota: running %s, latest %s%s\n", CS_VERSION, tag, avail ? " (update available)" : "");
    setState(ST_IDLE);
}

bool installFail(const char *msg) {
    Serial.printf("[cs] ota: %s\n", msg);
    setState(ST_ERROR, msg);
    return false;
}

bool doInstall() {
    if (!supported()) return installFail("OTA not supported on this flash layout: reflash over USB");
    doCheck();
    Status st = snapshot();
    if (st.state == ST_ERROR) return false;
    if (!st.updateAvailable) return installFail("Already up to date");

    char asset[96], url[192], sigUrl[200], msg[96];
    if (!otaAssetName(CS_PIO_ENV, st.latest, asset, sizeof asset)) return installFail("No release image for this board");
    snprintf(url, sizeof url, "%s/%s/%s", kBaseUrl, st.latest, asset);
    snprintf(sigUrl, sizeof sigUrl, "%s.sig", url);

    setState(ST_DOWNLOADING);
    setProgress(0, 0);
    // Signature first: no signature, nothing touches the OTA slot.
    uint8_t sig[96];
    size_t sigLen = 0;
    {
        WiFiClient client;
        HTTPClient http;
        beginHttp(http, client, sigUrl, kHttpTimeoutMs);
        const int code = http.GET();
        if (code == HTTP_CODE_OK) sigLen = readBody(http, sig, sizeof sig);
        http.end();
        if (code != HTTP_CODE_OK) {
            snprintf(msg, sizeof msg, "Signature download: HTTP %d", code);
            return installFail(msg);
        }
        if (!sigLen) return installFail("Signature download: empty");
    }

    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (!next) return installFail("No OTA slot");
    WiFiClient client;
    HTTPClient http;
    beginHttp(http, client, url, 30000);
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        snprintf(msg, sizeof msg, "Image download: HTTP %d", code);
        return installFail(msg);
    }
    const int len = http.getSize();
    if (len > 0 && (size_t)len > next->size) {
        http.end();
        return installFail("Image is larger than the app slot: reflash over USB");
    }
    if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
        http.end();
        snprintf(msg, sizeof msg, "Update.begin failed (%u)", (unsigned)Update.getError());
        return installFail(msg);
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    WiFiClient &stream = http.getStream();
    static uint8_t chunk[2048];
    size_t written = 0;
    uint32_t lastMs = millis();
    bool failed = false;
    while (len <= 0 || written < (size_t)len) {
        size_t avail = stream.available();
        if (!avail) {
            if (!http.connected()) break;   // closed: done, or short (caught below)
            if (millis() - lastMs > kStallMs) { failed = true; snprintf(msg, sizeof msg, "Download stalled"); break; }
            delay(10);
            continue;
        }
        if (avail > sizeof chunk) avail = sizeof chunk;
        int n = stream.readBytes(chunk, avail);
        if (n <= 0) continue;
        if (Update.write(chunk, (size_t)n) != (size_t)n) { failed = true; snprintf(msg, sizeof msg, "Flash write failed"); break; }
        mbedtls_sha256_update(&sha, chunk, (size_t)n);
        written += (size_t)n;
        lastMs = millis();
        setProgress(written, len > 0 ? (uint32_t)len : 0);
    }
    http.end();
    if (!failed && len > 0 && written != (size_t)len) { failed = true; snprintf(msg, sizeof msg, "Download incomplete"); }
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (failed) { Update.abort(); return installFail(msg); }

    setState(ST_VERIFYING);
    if (!verifySignature(digest, sig, sigLen)) { Update.abort(); return installFail("Signature verification failed"); }
    if (!Update.end() || !Update.isFinished()) {
        snprintf(msg, sizeof msg, "Update.end failed (%u)", (unsigned)Update.getError());
        return installFail(msg);
    }
    Serial.printf("[cs] ota: installed %s (%u bytes), restarting\n", st.latest, (unsigned)written);
    setState(ST_INSTALLED);
    return true;
}

void taskMain(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const Job job = s_job;
        s_job = JOB_NONE;
        if (WiFi.status() != WL_CONNECTED) setState(ST_ERROR, "WiFi not connected");
        else if (job == JOB_CHECK) doCheck();
        else if (job == JOB_INSTALL) doInstall();
        s_busy = false;
    }
}

bool start(Job job) {
    if (!s_task || s_busy || s_st.state == ST_INSTALLED) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    s_busy = true;
    s_job = job;
    xTaskNotifyGive(s_task);
    return true;
}

}  // namespace

void otaBegin(const Settings *settings) {
    s_cfg = settings;
    // Core 0 next to the WiFi stack; the Arduino loop (radio, web) is on core 1.
    xTaskCreatePinnedToCore(taskMain, "ota", 8192, nullptr, 1, &s_task, 0);
    Serial.printf("[cs] ota: %s, env %s\n", supported() ? "ready" : "unsupported", CS_PIO_ENV[0] ? CS_PIO_ENV : "?");
}

bool otaRequestCheck() { return start(JOB_CHECK); }
bool otaRequestInstall() { return supported() && start(JOB_INSTALL); }
bool otaRestartPending() { return s_st.state == ST_INSTALLED; }

void otaLoop(uint32_t nowMs) {
    if (WiFi.status() != WL_CONNECTED) { s_wifiUpAtMs = 0; return; }
    if (!s_wifiUpAtMs) {
        s_wifiUpAtMs = nowMs ? nowMs : 1;
        if (!s_nextCheckAtMs) s_nextCheckAtMs = nowMs + kFirstCheckDelayMs;
    }
    if (s_busy || !CS_PIO_ENV[0]) return;
    const Status st = snapshot();
    if ((int32_t)(nowMs - s_nextCheckAtMs) >= 0) {
        s_nextCheckAtMs = nowMs + kRecheckMs;
        if (otaRequestCheck()) return;
    }
    if (s_checkFailed) {   // try again sooner than the daily check
        s_checkFailed = false;
        s_nextCheckAtMs = nowMs + kRetryMs;
    }
    // Unattended install: only for a real release build, once per offered tag.
    if (s_cfg && s_cfg->otaAutoInstall && st.updateAvailable && !s_autoTried && otaVersionKnown(CS_VERSION)) {
        s_autoTried = true;
        Serial.printf("[cs] ota: auto-installing %s\n", st.latest);
        otaRequestInstall();
    }
}

String otaStatusJson() {
    const Status st = snapshot();
    String j = String("{\"state\":\"") + kStateNames[st.state] + "\",\"current\":\"" + CS_VERSION + "\",\"latest\":";
    j += st.checked ? "\"" + String(st.latest) + "\"" : String("null");
    j += ",\"updateAvailable\":" + String(st.updateAvailable ? "true" : "false");
    j += ",\"checkedAgoSec\":" + (st.checked ? String((millis() - st.checkedAtMs) / 1000) : String("null"));
    j += ",\"written\":" + String(st.written) + ",\"total\":" + String(st.total);
    String err;
    for (const char *p = st.error; *p; p++) {
        if (*p == '"' || *p == '\\') err += '\\';
        err += *p;
    }
    j += ",\"error\":\"" + err + "\",\"autoInstall\":" + String(s_cfg && s_cfg->otaAutoInstall ? "true" : "false");
    j += ",\"supported\":" + String(supported() ? "true" : "false") + ",\"wifi\":" +
         String(WiFi.status() == WL_CONNECTED ? "true" : "false") + "}";
    return j;
}
