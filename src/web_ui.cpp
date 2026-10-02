#include "web_ui.h"
#include <WebServer.h>
#include "mesh_channel_plan.h"

static WebServer               s_server(80);
static Settings               *s_cfg = nullptr;
static std::function<void()>   s_onSaved;
static std::function<String()> s_status;

static String esc(const char *in) {
    String o;
    for (const char *p = in; *p; p++) {
        switch (*p) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default:  o += *p;
        }
    }
    return o;
}

static String textInput(const char *label, const char *name, const char *value, int maxLen,
                        const char *type = "text") {
    return String("<label>") + label + "<input type=\"" + type + "\" name=\"" + name +
           "\" maxlength=\"" + maxLen + "\" value=\"" + esc(value) + "\"></label>";
}

static String numInput(const char *label, const char *name, long value, long lo, long hi) {
    return String("<label>") + label + "<input type=\"number\" name=\"" + name + "\" min=\"" + lo +
           "\" max=\"" + hi + "\" value=\"" + value + "\"></label>";
}

static String renderPage(const Settings &c, const char *error) {
    String h;
    h.reserve(9000);
    h += F("<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>Camillia Chat Server</title><style>"
           "body{font-family:system-ui,sans-serif;max-width:640px;margin:0 auto;padding:16px;"
           "background:#fff;color:#111}@media(prefers-color-scheme:dark){body{background:#111;color:#eee}"
           "input,select{background:#222;color:#eee;border-color:#444}}"
           "fieldset{margin:0 0 16px;border:1px solid #8884;border-radius:6px}"
           "label{display:block;margin:6px 0}input,select{display:block;width:100%;box-sizing:border-box;"
           "padding:6px;border:1px solid #aaa;border-radius:4px}input[type=checkbox]{display:inline;width:auto}"
           ".err{background:#c0392b;color:#fff;padding:8px;border-radius:4px}"
           "button{padding:10px 20px;font-size:16px}pre{white-space:pre-wrap;font-size:13px}"
           "</style></head><body><h1>Camillia Chat Server</h1>");
    if (error && error[0]) h += String("<p class=\"err\">") + esc(error) + "</p>";
    h += F("<form method=\"post\" action=\"/save\">");

    h += F("<fieldset><legend>Identity</legend>");
    h += textInput("Node name", "longName", c.longName, 24);
    h += textInput("Short name (up to 4)", "shortName", c.shortName, 4);
    h += F("</fieldset>");

    h += F("<fieldset><legend>Radio</legend><label>Region<select name=\"region\">");
    for (uint8_t i = 0; i < kRegionCount; i++) {
        h += String("<option") + (strcmp(kRegions[i].code, c.region) == 0 ? " selected" : "") + ">" +
             kRegions[i].code + "</option>";
    }
    h += F("</select></label><label>Preset<select name=\"preset\">");
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (!presetUsableOnThisRadio(i)) continue;
        h += String("<option value=\"") + i + "\"" + (i == c.modemPreset ? " selected" : "") + ">" +
             kPresets[i].name + "</option>";
    }
    h += F("</select></label>");
    h += numInput("Frequency slot (0 = automatic)", "slot", c.freqSlot, 0, 255);
    h += F("</fieldset>");

    h += F("<fieldset><legend>Channels</legend>");
    h += numInput("Number of channels", "chanCount", c.chanCount, 1, 3);
    for (int i = 0; i < 3; i++) {
        char nameField[8], keyField[8], label[24], key[48];
        snprintf(nameField, sizeof nameField, "cn%d", i);
        snprintf(keyField, sizeof keyField, "ck%d", i);
        formatKeyBase64(c.ch[i].key, c.ch[i].keyLen, key, sizeof key);
        snprintf(label, sizeof label, "Channel %d name", i + 1);
        h += textInput(label, nameField, c.ch[i].name, 11);
        snprintf(label, sizeof label, "Channel %d key (base64)", i + 1);
        h += textInput(label, keyField, key, 47);
    }
    h += F("</fieldset>");

    h += F("<fieldset><legend>Replies</legend>");
    h += numInput("Batch size (messages)", "batch", c.batchSize, 1, 50);
    h += numInput("Gap between packets (ms)", "gap", c.packetGapMs, 500, 60000);
    h += numInput("Max hops", "hops", c.maxHops, 0, 7);
    h += F("</fieldset>");

    h += F("<fieldset><legend>MQTT (receive only)</legend><label><input type=\"checkbox\" name=\"mqttOn\"");
    if (c.mqttEnabled) h += F(" checked");
    h += F("> Enabled</label>");
    h += textInput("Broker", "mqttHost", c.mqttHost, 63);
    h += numInput("Port", "mqttPort", c.mqttPort, 1, 65535);
    h += textInput("Username", "mqttUser", c.mqttUser, 31);
    h += textInput("Password", "mqttPass", c.mqttPass, 63, "password");
    h += textInput("Topic root", "mqttRoot", c.mqttRoot, 47);
    h += F("</fieldset>");

    h += F("<fieldset><legend>WiFi</legend>");
    h += textInput("Access point password (empty = open)", "apPass", c.apPass, 63, "password");
    h += textInput("Home WiFi SSID (optional)", "staSsid", c.staSsid, 32);
    h += textInput("Home WiFi password", "staPass", c.staPass, 63, "password");
    h += textInput("Time zone (POSIX TZ, for the screen clock)", "tz", c.tz, 47);
    h += F("</fieldset><button type=\"submit\">Save and restart</button></form>");

    h += F("<h2>Status</h2><pre id=\"st\">loading…</pre><script>"
           "async function s(){try{const r=await fetch('/status');"
           "document.getElementById('st').textContent=JSON.stringify(await r.json(),null,2)}catch(e){}}"
           "s();setInterval(s,5000)</script></body></html>");
    return h;
}

static void copyArg(const char *name, char *dst, size_t cap) {
    if (!s_server.hasArg(name)) return;
    strncpy(dst, s_server.arg(name).c_str(), cap - 1);
    dst[cap - 1] = 0;
}

static void handleSave() {
    Settings n = *s_cfg;
    char err[96] = "";
    copyArg("longName", n.longName, sizeof n.longName);
    copyArg("shortName", n.shortName, sizeof n.shortName);
    copyArg("region", n.region, sizeof n.region);
    n.modemPreset = (uint8_t)s_server.arg("preset").toInt();
    n.freqSlot = (uint8_t)s_server.arg("slot").toInt();
    n.chanCount = (uint8_t)s_server.arg("chanCount").toInt();
    for (int i = 0; i < 3; i++) {
        char f[8];
        snprintf(f, sizeof f, "cn%d", i);
        copyArg(f, n.ch[i].name, sizeof n.ch[i].name);
        snprintf(f, sizeof f, "ck%d", i);
        if (!parseKeyBase64(s_server.arg(f).c_str(), n.ch[i].key, n.ch[i].keyLen) && i < n.chanCount)
            snprintf(err, sizeof err, "Channel %d key is not a valid base64 key", i + 1);
    }
    long batch = s_server.arg("batch").toInt(), gap = s_server.arg("gap").toInt(),
         hops = s_server.arg("hops").toInt();
    n.batchSize = (uint8_t)constrain(batch, 0, 255);
    n.packetGapMs = (uint16_t)constrain(gap, 0, 65535);
    n.maxHops = (uint8_t)constrain(hops, 0, 255);
    n.mqttEnabled = s_server.hasArg("mqttOn");
    copyArg("mqttHost", n.mqttHost, sizeof n.mqttHost);
    n.mqttPort = (uint16_t)s_server.arg("mqttPort").toInt();
    copyArg("mqttUser", n.mqttUser, sizeof n.mqttUser);
    copyArg("mqttPass", n.mqttPass, sizeof n.mqttPass);
    copyArg("mqttRoot", n.mqttRoot, sizeof n.mqttRoot);
    copyArg("apPass", n.apPass, sizeof n.apPass);
    copyArg("staSsid", n.staSsid, sizeof n.staSsid);
    copyArg("staPass", n.staPass, sizeof n.staPass);
    copyArg("tz", n.tz, sizeof n.tz);

    if (!err[0]) settingsValidate(n, err, sizeof err);
    if (err[0]) {
        s_server.send(400, "text/html", renderPage(n, err));
        return;
    }
    *s_cfg = n;
    settingsSave(n);
    s_server.send(200, "text/html",
                  F("<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" "
                    "content=\"width=device-width\"><body style=\"font-family:sans-serif\">"
                    "<p>Saved. Restarting…</p><script>setTimeout(()=>location='/',8000)</script>"));
    if (s_onSaved) s_onSaved();
}

void webBegin(Settings *settings, std::function<void()> onSaved, std::function<String()> statusJson) {
    s_cfg = settings;
    s_onSaved = onSaved;
    s_status = statusJson;
    s_server.on("/", HTTP_GET, [] { s_server.send(200, "text/html", renderPage(*s_cfg, nullptr)); });
    s_server.on("/save", HTTP_POST, handleSave);
    s_server.on("/status", HTTP_GET, [] {
        s_server.send(200, "application/json", s_status ? s_status() : String("{}"));
    });
    s_server.begin();
}

void webLoop() { s_server.handleClient(); }
