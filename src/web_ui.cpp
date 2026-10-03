#include "web_ui.h"
#include <WebServer.h>
#include "mesh_channel_plan.h"
#include "settings_yaml.h"

static WebServer               s_server(80);
static Settings               *s_cfg = nullptr;
static std::function<void()>   s_onSaved;
static std::function<String()> s_status;
static MessageLister           s_listMessages;
static MessageClearer          s_clearMessages;
static std::function<String()> s_storageJson;

static const char *presetName(uint8_t i) { return i < PRESET_COUNT ? kPresets[i].channelName : nullptr; }
static int presetIndex(const char *name) {
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (!strcmp(kPresets[i].channelName, name) || !strcmp(kPresets[i].name, name))
            return presetUsableOnThisRadio(i) ? i : -1;
    }
    return -1;
}
static const YamlPresetMap kPresetMap{presetName, presetIndex};

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
           ".tabs{display:flex;gap:4px;margin:0 0 16px;border-bottom:1px solid #8886}"
           ".tabs button{border:0;background:none;color:inherit;padding:8px 16px;cursor:pointer;"
           "border-bottom:3px solid transparent}.tabs button.on{border-bottom-color:#2a7ae2;font-weight:600}"
           ".msg{border-bottom:1px solid #8884;padding:8px 0}.meta{font-size:12px;opacity:.7}"
           ".txt{white-space:pre-wrap;word-break:break-word}.row{display:flex;gap:8px;align-items:end}"
           ".row label{flex:1}.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));"
           "gap:8px;margin:0 0 16px}.stat{border:1px solid #8884;border-radius:6px;padding:8px}"
           ".stat b{display:block;font-size:20px}.bar{height:6px;background:#8883;border-radius:3px;margin-top:6px}"
           ".bar i{display:block;height:100%;background:#2a7ae2;border-radius:3px}"
           "</style></head><body><h1>Camillia Chat Server</h1>"
           "<nav class=\"tabs\"><button data-tab=\"config\">Config</button>"
           "<button data-tab=\"messages\">Messages</button></nav><section id=\"config\">");
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
    h += numInput("Number of channels (1-10)", "chanCount", c.chanCount, 1, SETTINGS_MAX_CHANNELS);
    for (int i = 0; i < SETTINGS_MAX_CHANNELS; i++) {
        char nameField[8], keyField[8], label[24], key[48];
        snprintf(nameField, sizeof nameField, "cn%d", i);
        snprintf(keyField, sizeof keyField, "ck%d", i);
        formatKeyBase64(c.ch[i].key, c.ch[i].keyLen, key, sizeof key);
        h += String("<div class=\"chan\" data-i=\"") + i + "\">";
        snprintf(label, sizeof label, "Channel %d name", i + 1);
        h += textInput(label, nameField, c.ch[i].name, 11);
        snprintf(label, sizeof label, "Channel %d key (base64)", i + 1);
        h += textInput(label, keyField, key, 47);
        h += "</div>";
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
    h += textInput("Home WiFi SSID (optional)", "staSsid", c.staSsid, 32);
    h += textInput("Home WiFi password", "staPass", c.staPass, 63, "password");
    h += textInput("Time zone (POSIX TZ, for the screen clock)", "tz", c.tz, 47);
    h += F("</fieldset><button type=\"submit\">Save and restart</button></form>");

    h += F("<h2>Backup</h2><p><a href=\"/export.yaml\">Export config (YAML)</a> &mdash; "
           "the file includes channel keys and passwords.</p>"
           "<form method=\"post\" action=\"/import\"><fieldset><legend>Import config (YAML)</legend>"
           "<label>File<input type=\"file\" accept=\".yaml,.yml,text/yaml,text/plain\" "
           "onchange=\"const f=this.files[0];if(f)f.text().then(t=>this.form.yaml.value=t)\"></label>"
           "<label>Or paste<textarea name=\"yaml\" rows=\"8\" style=\"width:100%;box-sizing:border-box;"
           "font-family:monospace\"></textarea></label>"
           "<button type=\"submit\">Import and restart</button></fieldset></form>");

    h += F("<h2>Status</h2><pre id=\"st\">loading…</pre></section>"
           "<section id=\"messages\" hidden><div id=\"stats\" class=\"stats\"></div>"
           "<div class=\"row\"><label>Channel<select id=\"ch\">");
    for (int i = 0; i < c.chanCount; i++) {
        h += String("<option value=\"") + i + "\">" + esc(c.ch[i].name) + "</option>";
    }
    h += F("</select></label><button id=\"refresh\" type=\"button\">Refresh</button></div>"
           "<div class=\"row\" style=\"margin-top:8px\"><button id=\"clear1\" type=\"button\">Clear this channel</button>"
           "<button id=\"clearAll\" type=\"button\">Clear all channels</button></div>"
           "<p id=\"count\" class=\"meta\"></p><div id=\"list\"></div></section><script>"
           "const $=id=>document.getElementById(id);"
           "async function s(){try{const r=await fetch('/status');"
           "$('st').textContent=JSON.stringify(await r.json(),null,2)}catch(e){}}"
           "function when(m){if(m.rxUnix)return new Date(m.rxUnix*1000).toLocaleString();"
           "if(m.ageSec===null)return'before last restart';const a=m.ageSec;"
           "return a<60?a+'s ago':a<3600?Math.floor(a/60)+'m ago':Math.floor(a/3600)+'h ago'}"
           "function card(title,big,sub,pct){const d=document.createElement('div');d.className='stat';"
           "const t=document.createElement('div');t.className='meta';t.textContent=title;const b=document.createElement('b');"
           "b.textContent=big;const s=document.createElement('div');s.className='meta';s.textContent=sub;d.append(t,b,s);"
           "if(pct!==undefined){const bar=document.createElement('div');bar.className='bar';const i=document.createElement('i');"
           "i.style.width=Math.min(100,pct)+'%';bar.append(i);d.append(bar)}return d}"
           "function ts(u){return u?new Date(u*1000).toLocaleString():'unknown time'}"
           "function kb(b){return b<1024?b+' B':(b/1024).toFixed(1)+' KB'}"
           "async function stats(){try{const r=await fetch('/storage');const st=await r.json();const cards=[];"
           "let total=0,cap=0,ram=0,text=0,file=0;for(const c of st.channels){total+=c.count;cap+=c.capacity;"
           "ram+=c.ramBytes;text+=c.textBytes;file+=c.fileBytes;"
           "cards.push(card(c.name,c.count+' / '+c.capacity,(c.count?('oldest '+ts(c.oldestUnix)+', newest '+ts(c.newestUnix)+'. '):'Empty. ')"
           "+kb(c.textBytes)+' of text in '+kb(c.ramBytes)+' RAM, '+kb(c.fileBytes)+' on flash',100*c.count/c.capacity))}"
           "cards.unshift(card('Messages stored',total+' / '+cap,st.channels.length+' channel'+(st.channels.length==1?'':'s'),100*total/cap),"
           "card('RAM for messages',kb(ram),kb(text)+' of message text',100*text/Math.max(ram,1)));"
           "cards.push(card('Message files',kb(file),'saved on flash'));"
           "cards.push(card('Flash',Math.round(st.flash.usedKB)+' KB',' of '+Math.round(st.flash.totalKB)+' KB used',100*st.flash.usedKB/st.flash.totalKB));"
           "cards.push(card('PSRAM',Math.round(st.psram.freeKB)+' KB free','of '+Math.round(st.psram.totalKB)+' KB'));"
           "$('stats').replaceChildren(...cards)}catch(e){}}"
           "async function load(){stats();const l=$('list');try{const r=await fetch('/messages?ch='+$('ch').value);"
           "const ms=await r.json();$('count').textContent=ms.length+' stored message'+(ms.length==1?'':'s')+', newest first';"
           "l.replaceChildren(...ms.map(m=>{const d=document.createElement('div');d.className='msg';"
           "const meta=document.createElement('div');meta.className='meta';"
           "meta.textContent=when(m)+' \u00b7 '+m.from+' \u00b7 '+m.source+' \u00b7 #'+m.seq;"
           "const t=document.createElement('div');t.className='txt';t.textContent=m.text;"
           "d.append(meta,t);return d}))}catch(e){$('count').textContent='Could not load messages'}}"
           "function show(tab){for(const b of document.querySelectorAll('.tabs button'))"
           "b.classList.toggle('on',b.dataset.tab==tab);$('config').hidden=tab!='config';"
           "$('messages').hidden=tab!='messages';if(tab=='messages')load();history.replaceState(null,'','#'+tab)}"
           "for(const b of document.querySelectorAll('.tabs button'))b.onclick=()=>show(b.dataset.tab);"
           "$('ch').onchange=load;$('refresh').onclick=load;"
           "const cc=document.querySelector('[name=chanCount]');function rows(){const n=Math.max(3,+cc.value||1);"
           "for(const d of document.querySelectorAll('.chan'))d.hidden=+d.dataset.i>=n}cc.oninput=rows;rows();"
           "async function clr(ch,what){if(!confirm('Delete all stored messages on '+what+'? Nodes will no longer be able to catch up on them.'))return;"
           "await fetch('/clear',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ch='+ch});load();s()}"
           "$('clear1').onclick=()=>clr($('ch').value,$('ch').selectedOptions[0].text);"
           "$('clearAll').onclick=()=>clr('all','every channel');"
           "setInterval(()=>{if(!$('messages').hidden)load()},15000);"
           "show(location.hash=='#messages'?'messages':'config');s();setInterval(s,5000)"
           "</script></body></html>");
    return h;
}

static void copyArg(const char *name, char *dst, size_t cap) {
    if (!s_server.hasArg(name)) return;
    strncpy(dst, s_server.arg(name).c_str(), cap - 1);
    dst[cap - 1] = 0;
}

static void commit(const Settings &n);

static void handleSave() {
    Settings n = *s_cfg;
    char err[96] = "";
    copyArg("longName", n.longName, sizeof n.longName);
    copyArg("shortName", n.shortName, sizeof n.shortName);
    copyArg("region", n.region, sizeof n.region);
    n.modemPreset = (uint8_t)s_server.arg("preset").toInt();
    n.freqSlot = (uint8_t)s_server.arg("slot").toInt();
    n.chanCount = (uint8_t)s_server.arg("chanCount").toInt();
    for (int i = 0; i < SETTINGS_MAX_CHANNELS; i++) {
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
    copyArg("staSsid", n.staSsid, sizeof n.staSsid);
    copyArg("staPass", n.staPass, sizeof n.staPass);
    copyArg("tz", n.tz, sizeof n.tz);

    if (!err[0]) settingsValidate(n, err, sizeof err);
    if (err[0]) {
        s_server.send(400, "text/html", renderPage(n, err));
        return;
    }
    commit(n);
}

static void handleExport() {
    String name = String("camillia-cs-") + s_cfg->shortName + ".yaml";
    s_server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    s_server.send(200, "application/x-yaml", settingsToYaml(*s_cfg, kPresetMap).c_str());
}

static void handleImport() {
    Settings n = *s_cfg;
    char err[96] = "";
    if (!settingsFromYaml(s_server.arg("yaml").c_str(), n, kPresetMap, err, sizeof err)) {
        String msg = String("Import failed: ") + err;
        s_server.send(400, "text/html", renderPage(*s_cfg, msg.c_str()));
        return;
    }
    commit(n);
}

static void commit(const Settings &n) {
    *s_cfg = n;
    settingsSave(n);
    s_server.send(200, "text/html",
                  F("<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" "
                    "content=\"width=device-width\"><body style=\"font-family:sans-serif\">"
                    "<p>Saved. Restarting…</p><script>setTimeout(()=>location='/',8000)</script>"));
    if (s_onSaved) s_onSaved();
}

static void handleMessages() {
    int slot = s_server.arg("ch").toInt();
    if (slot < 0 || slot >= s_cfg->chanCount || !s_listMessages) {
        s_server.send(200, "application/json", "[]");
        return;
    }
    s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server.send(200, "application/json", "");
    bool first = true;
    String chunk = "[";
    s_listMessages(slot, [&](const char *json) {
        if (!first) chunk += ",";
        first = false;
        chunk += json;
        if (chunk.length() > 2048) { s_server.sendContent(chunk); chunk = ""; }
    });
    chunk += "]";
    s_server.sendContent(chunk);
    s_server.sendContent("");
}

static void handleClear() {
    String ch = s_server.arg("ch");
    int slot = ch == "all" ? -1 : ch.toInt();
    if (!s_clearMessages || (ch != "all" && (ch.isEmpty() || slot < 0 || slot >= s_cfg->chanCount))) {
        s_server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    s_clearMessages(slot);
    s_server.send(200, "application/json", "{\"ok\":true}");
}

void webBegin(Settings *settings, std::function<void()> onSaved, std::function<String()> statusJson,
              MessageLister listMessages, MessageClearer clearMessages, std::function<String()> storageJson) {
    s_cfg = settings;
    s_storageJson = storageJson;
    s_clearMessages = clearMessages;
    s_listMessages = listMessages;
    s_onSaved = onSaved;
    s_status = statusJson;
    s_server.on("/", HTTP_GET, [] { s_server.send(200, "text/html", renderPage(*s_cfg, nullptr)); });
    s_server.on("/save", HTTP_POST, handleSave);
    s_server.on("/export.yaml", HTTP_GET, handleExport);
    s_server.on("/import", HTTP_POST, handleImport);
    s_server.on("/messages", HTTP_GET, handleMessages);
    s_server.on("/clear", HTTP_POST, handleClear);
    s_server.on("/storage", HTTP_GET, [] {
        s_server.send(200, "application/json", s_storageJson ? s_storageJson() : String("{}"));
    });
    s_server.on("/status", HTTP_GET, [] {
        s_server.send(200, "application/json", s_status ? s_status() : String("{}"));
    });
    s_server.begin();
}

void webLoop() { s_server.handleClient(); }
