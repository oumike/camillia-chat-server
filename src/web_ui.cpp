#include "web_ui.h"
#include <WebServer.h>
#include "mesh_channel_plan.h"
#include "settings_yaml.h"
#include "radio_link.h"
#include "ota_update.h"
#include "persistence.h"

static WebServer               s_server(80);
static Settings               *s_cfg = nullptr;
static std::function<void()>   s_onSaved;
static std::function<String()> s_status;
static MessageLister           s_listMessages;
static MessageClearer          s_clearMessages;
static std::function<String()> s_storageJson;
static std::function<bool()>   s_announce;

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

static String field(const char *label, const String &control, const char *hint = nullptr) {
    String o = String("<label class=\"field\"><span>") + label + "</span>" + control;
    if (hint) o += String("<small>") + hint + "</small>";
    return o + "</label>";
}

static String textInput(const char *label, const char *name, const char *value, int maxLen,
                        const char *type = "text", const char *hint = nullptr) {
    return field(label, String("<input type=\"") + type + "\" name=\"" + name + "\" maxlength=\"" + maxLen +
                            "\" value=\"" + esc(value) + "\" autocomplete=\"off\">", hint);
}

static String numInput(const char *label, const char *name, long value, long lo, long hi,
                       const char *hint = nullptr) {
    return field(label, String("<input type=\"number\" name=\"") + name + "\" min=\"" + lo + "\" max=\"" + hi +
                            "\" value=\"" + value + "\">", hint);
}

static String toggle(const char *label, const char *name, bool on, const char *hint) {
    return String("<label class=\"toggle\"><input type=\"checkbox\" name=\"") + name + "\"" + (on ? " checked" : "") +
           "><i></i><span><b>" + label + "</b><small>" + hint + "</small></span></label>";
}

static const char kStyle[] = R"CSS(
:root{--bg:#f4f5f8;--card:#fff;--fg:#1b1f24;--dim:#5f6b7a;--line:#e2e5ea;--input:#fff;--accent:#d85070;
--accent-soft:#d8507018;--ok:#2e9e5b;--warn:#c98a12;--bad:#d64545;--shadow:0 1px 2px #0000000d,0 4px 16px #00000008;
color-scheme:light dark}
@media(prefers-color-scheme:dark){:root{--bg:#0d1015;--card:#161a21;--fg:#e8ecf1;--dim:#97a2b1;--line:#262c36;
--input:#0f1218;--accent-soft:#d8507026;--shadow:0 1px 2px #0006}}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--fg)}
header.top{position:sticky;top:0;z-index:5;background:var(--card);border-bottom:1px solid var(--line);box-shadow:var(--shadow)}
.wrap{max-width:880px;margin:0 auto;padding:0 16px}
.bar{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:12px 0 8px}
.brand{display:flex;align-items:center;gap:10px;margin-right:auto}
.brand svg{width:34px;height:34px;flex:none}
.brand b{display:block;font-size:17px;letter-spacing:.2px}.brand small{display:block;color:var(--dim);font-size:12px}
.pills{display:flex;gap:6px;flex-wrap:wrap}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:12px;padding:4px 10px;border-radius:999px;
background:var(--bg);border:1px solid var(--line);white-space:nowrap}
.pill i{width:8px;height:8px;border-radius:50%;background:var(--dim)}
.pill.ok i{background:var(--ok)}.pill.warn i{background:var(--warn)}.pill.bad i{background:var(--bad)}
nav.tabs{display:flex;gap:4px}
nav.tabs button{font:inherit;border:0;background:none;color:var(--dim);padding:10px 14px;cursor:pointer;
border-bottom:3px solid transparent;font-weight:600}
nav.tabs button.on{color:var(--fg);border-bottom-color:var(--accent)}
main{padding:20px 0 96px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;box-shadow:var(--shadow);padding:18px 18px 8px;margin:0 0 16px}
.card>h2{display:flex;align-items:center;gap:8px;font-size:15px;margin:0 0 4px}
.card>h2 .ico{width:28px;height:28px;border-radius:8px;display:grid;place-items:center;background:var(--accent-soft);color:var(--accent);font-size:15px}
.card>p.sub{margin:0 0 14px;color:var(--dim);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:0 14px}
.field{display:block;margin:0 0 14px}
.field>span{display:block;font-size:12px;font-weight:600;color:var(--dim);margin:0 0 5px;text-transform:uppercase;letter-spacing:.4px}
.field small{display:block;color:var(--dim);font-size:12px;margin-top:4px}
input,select,textarea{font:inherit;color:var(--fg);background:var(--input);width:100%;padding:9px 11px;
border:1px solid var(--line);border-radius:9px;outline:none;transition:border-color .15s,box-shadow .15s}
input:focus,select:focus,textarea:focus{border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}
input.badkey{border-color:var(--bad)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px}
.btn{font:inherit;font-weight:600;display:inline-flex;align-items:center;gap:6px;padding:9px 16px;border-radius:9px;
border:1px solid var(--line);background:var(--card);color:var(--fg);cursor:pointer;text-decoration:none}
.btn:hover{border-color:var(--accent)}.btn:disabled{opacity:.5;cursor:default}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#fff}
.btn.danger{color:var(--bad)}.btn.danger:hover{border-color:var(--bad)}
.btn.sm{padding:6px 10px;font-size:13px}
.btn.xs{padding:3px 10px;font-size:12px;border-radius:999px}
.pills{align-items:center}
.icon{font:inherit;width:36px;height:36px;flex:none;border-radius:9px;border:1px solid var(--line);background:var(--card);
color:var(--dim);cursor:pointer;display:grid;place-items:center}
.icon:hover{color:var(--accent);border-color:var(--accent)}.icon.del:hover{color:var(--bad);border-color:var(--bad)}
.err{background:#d6454518;border:1px solid var(--bad);color:var(--bad);padding:10px 14px;border-radius:10px;margin:0 0 16px;font-weight:600}
.chans{display:flex;flex-direction:column;gap:10px;margin:0 0 12px}
.chan{display:grid;grid-template-columns:34px minmax(110px,1fr) minmax(180px,2.2fr) 36px;gap:10px;align-items:start;
padding:12px;border:1px solid var(--line);border-radius:12px;background:var(--bg)}
.chan .field{margin:0}
.chan .idx{width:34px;height:34px;border-radius:50%;display:grid;place-items:center;font-weight:700;font-size:13px;
background:var(--accent-soft);color:var(--accent);margin-top:20px}
.chan .del{margin-top:20px}
.keybox{display:flex;gap:6px}
.tag{display:inline-block;font-style:normal;font-size:11px;font-weight:700;padding:1px 8px;border-radius:999px;background:var(--line);color:var(--dim)}
.tag.ok{background:#2e9e5b22;color:var(--ok)}.tag.bad{background:#d6454522;color:var(--bad)}.tag.warn{background:#c98a1222;color:var(--warn)}
.chanfoot{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:0 0 14px;color:var(--dim);font-size:13px}
.chan>*,.grid>*,.keybox input{min-width:0}.grid{grid-template-columns:repeat(auto-fit,minmax(min(220px,100%),1fr))}@media(max-width:620px){.chan{grid-template-columns:34px 1fr 36px}.chan .k{grid-column:2/3}.chan .idx,.chan .del{margin-top:0}}
.toggle{display:flex;gap:12px;align-items:flex-start;margin:0 0 14px;cursor:pointer}
.toggle input{display:none}
.toggle i{flex:none;width:40px;height:22px;border-radius:999px;background:var(--line);position:relative;transition:background .15s;margin-top:1px}
.toggle i:after{content:"";position:absolute;top:3px;left:3px;width:16px;height:16px;border-radius:50%;background:#fff;transition:left .15s;box-shadow:0 1px 2px #0004}
.toggle input:checked+i{background:var(--accent)}.toggle input:checked+i:after{left:21px}
.toggle b{display:block;font-weight:600}.toggle small{display:block;color:var(--dim);font-size:12px}
.fw{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin:0 0 14px}
.kv{background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:10px 12px}
.kv span{display:block;font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.4px;color:var(--dim)}
.kv b{display:block;font-size:17px;margin-top:2px;word-break:break-word}
.kv small{display:block;color:var(--dim);font-size:12px}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 14px;align-items:center}
.prog{height:8px;background:var(--line);border-radius:4px;overflow:hidden;margin:0 0 14px}
.prog i{display:block;height:100%;width:0;background:var(--accent);transition:width .3s}
.savebar{position:fixed;left:0;right:0;bottom:0;z-index:5;background:var(--card);border-top:1px solid var(--line);box-shadow:0 -4px 16px #0000000d}
.savebar .wrap{display:flex;align-items:center;justify-content:space-between;gap:12px;padding-top:12px;padding-bottom:12px}
.savebar span{color:var(--dim);font-size:13px}.savebar.dirty span{color:var(--warn);font-weight:600}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px;margin:0 0 14px}
.meter{height:6px;background:var(--line);border-radius:3px;margin-top:8px;overflow:hidden}
.meter i{display:block;height:100%;background:var(--accent);border-radius:3px}
.toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:end;margin:0 0 12px}
.toolbar .field{margin:0;flex:1;min-width:160px}
.msgs{display:flex;flex-direction:column;gap:8px;margin:0 0 14px}
.msg{background:var(--bg);border:1px solid var(--line);border-radius:12px;padding:10px 12px}
.msg .meta{display:flex;gap:8px;flex-wrap:wrap;align-items:center;font-size:12px;color:var(--dim);margin:0 0 4px}
.msg .meta b{color:var(--fg)}
.msg .txt{white-space:pre-wrap;word-break:break-word}
.empty{color:var(--dim);text-align:center;padding:24px 0}
details{margin:0 0 14px}summary{cursor:pointer;color:var(--dim);font-size:13px}
pre{white-space:pre-wrap;font-size:12px;background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:10px}
a{color:var(--accent)}
)CSS";

static const char kLogo[] = R"SVG(<svg viewBox="0 0 40 40" aria-hidden="true"><circle cx="20" cy="20" r="19" fill="#d85070"/><g fill="#fff" opacity=".95"><ellipse cx="20" cy="11" rx="6" ry="7"/><ellipse cx="29" cy="18" rx="7" ry="6"/><ellipse cx="25" cy="28" rx="6" ry="7"/><ellipse cx="15" cy="28" rx="6" ry="7"/><ellipse cx="11" cy="18" rx="7" ry="6"/></g><circle cx="20" cy="20" r="5" fill="#f2c14e"/></svg>)SVG";

static const char kScript[] = R"JS(
const $=id=>document.getElementById(id),el=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e};
function dur(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+'d '+h+'h':h?h+'h '+m+'m':m?m+'m':s+'s'}
function kb(b){return b<1024?b+' B':(b/1024).toFixed(1)+' KB'}
function ts(u){return u?new Date(u*1000).toLocaleString():'unknown time'}
function pill(id,cls,text){const p=$(id);p.className='pill '+cls;p.lastChild.textContent=text}
let last={};
async function status(){try{const s=await (await fetch('/status')).json();last=s;
$('hdrNode').textContent=s.node+' \u00b7 '+s.nodeId;
pill('pIp',s.ip.startsWith('AP ')?'warn':'ok',s.ip.startsWith('AP ')?'Setup AP '+s.ip.slice(3):s.ip);
pill('pMqtt',s.mqtt=='connected'?'ok':s.mqtt=='off'?'':'warn','MQTT '+s.mqtt);
pill('pBatt',s.battery=='absent'?'':s.battery=='external'?'ok':s.batteryPct<20?'bad':s.batteryPct<50?'warn':'ok',
 s.battery=='absent'?'No battery':s.battery=='external'?'USB power':s.batteryPct+'% \u00b7 '+s.batteryV.toFixed(2)+' V');
if(!$('tabMonitor').hidden)overview(s);$('raw').textContent=JSON.stringify(s,null,2)}catch(e){pill('pIp','bad','Offline')}}
function stat(title,big,sub,pct){const d=el('div','kv');d.append(el('span','',title),el('b','',big));if(sub)d.append(el('small','',sub));
if(pct!==undefined){const m=el('div','meter'),i=el('i');i.style.width=Math.max(0,Math.min(100,pct))+'%';m.append(i);d.append(m)}return d}
function overview(s){const last=s.lastSent==null?'never':typeof s.lastSent=='number'?ts(s.lastSent):s.lastSent;
$('ov').replaceChildren(stat('Uptime',dur(s.uptimeSec),s.clockSet?'clock set':'clock not set'),
stat('Network',s.ip,s.ip.startsWith('AP ')?'config access point':'home WiFi'),
stat('MQTT',s.mqtt,s.mqttEnvelopes+' received \u00b7 '+s.mqttDecrypted+' decrypted'),
stat('Battery',s.battery=='absent'?'none':s.battery=='external'?'USB':s.batteryPct+'%',s.battery=='present'?s.batteryV.toFixed(2)+' V':'',s.battery=='present'?s.batteryPct:undefined),
stat('Last reply sent',last))}
async function stats(){try{const st=await (await fetch('/storage')).json();const cards=[];let total=0,cap=0,ram=0,text=0,file=0;
for(const c of st.channels){total+=c.count;cap+=c.capacity;ram+=c.ramBytes;text+=c.textBytes;file+=c.fileBytes;
cards.push(stat('#'+c.name,c.count+' / '+c.capacity,(c.count?'oldest '+ts(c.oldestUnix)+', newest '+ts(c.newestUnix)+'. ':'Empty. ')+kb(c.textBytes)+' text \u00b7 '+kb(c.fileBytes)+' on flash',100*c.count/c.capacity))}
cards.unshift(stat('Messages stored',total+' / '+cap,st.channels.length+' channel'+(st.channels.length==1?'':'s'),100*total/Math.max(cap,1)),
stat('RAM for messages',kb(ram),kb(text)+' of message text',100*text/Math.max(ram,1)));
cards.push(stat('Flash',Math.round(st.flash.usedKB)+' KB','of '+Math.round(st.flash.totalKB)+' KB \u00b7 '+kb(file)+' messages',100*st.flash.usedKB/Math.max(st.flash.totalKB,1)));
cards.push(stat('PSRAM free',Math.round(st.psram.freeKB)+' KB','of '+Math.round(st.psram.totalKB)+' KB'));
$('stats').replaceChildren(...cards)}catch(e){}}
function when(m){if(m.rxUnix)return new Date(m.rxUnix*1000).toLocaleString();if(m.ageSec===null)return'before last restart';return dur(m.ageSec)+' ago'}
async function load(){stats();const l=$('list');if(!$('ch').options.length){l.replaceChildren(el('div','empty','No channels configured'));return}
try{const ms=await (await fetch('/messages?ch='+$('ch').value)).json();
$('count').textContent=ms.length+' stored message'+(ms.length==1?'':'s')+', newest first';
if(!ms.length){l.replaceChildren(el('div','empty','No messages on this channel yet'));return}
l.replaceChildren(...ms.map(m=>{const d=el('div','msg'),meta=el('div','meta');
meta.append(el('b','',m.from),el('span','',when(m)),el('span','tag '+(m.source=='mqtt'?'warn':'ok'),m.source.toUpperCase()),el('span','','#'+m.seq));
d.append(meta,el('div','txt',m.text));return d}))}catch(e){$('count').textContent='Could not load messages'}}
async function clr(ch,what){if(!confirm('Delete all stored messages on '+what+'? Nodes will no longer be able to catch up on them.'))return;
await fetch('/clear',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ch='+ch});load();status()}
function show(tab){for(const b of document.querySelectorAll('nav.tabs button'))b.classList.toggle('on',b.dataset.tab==tab);
$('tabConfig').hidden=tab!='config';$('tabMonitor').hidden=tab!='monitor';$('savebar').hidden=tab!='config';
if(tab=='monitor'){load();if(last.node)overview(last)}history.replaceState(null,'','#'+tab)}
for(const b of document.querySelectorAll('nav.tabs button'))b.onclick=()=>show(b.dataset.tab);
$('ch').onchange=load;$('refresh').onclick=load;
$('clear1').onclick=()=>$('ch').value!==''&&clr($('ch').value,'#'+$('ch').selectedOptions[0].text);
$('clearAll').onclick=()=>clr('all','every channel');
$('announce').onclick=async()=>{const b=$('announce');let ok=false;b.disabled=true;
try{ok=(await fetch('/announce',{method:'POST'})).ok}catch(e){}
b.textContent=ok?'Announcement queued':'Could not queue';setTimeout(()=>{b.textContent='Send announcement';b.disabled=false},3000)};
// Channel editor
const cc=document.querySelector('[name=chanCount]'),rowsEl=[...document.querySelectorAll('.chan')],MAXC=rowsEl.length;
function keyLen(v){v=v.trim().replace(/=+$/,'');if(!v)return 0;if(!/^[A-Za-z0-9+/]+$/.test(v))return-1;return Math.floor(v.length*6/8)}
function keyInfo(r){const k=r.querySelector('.key'),t=r.querySelector('.keyinfo'),n=keyLen(k.value);
const [cls,txt]=n==0?['warn','No encryption']:n==1?['ok','Default Meshtastic key']:n==16?['ok','AES-128']:n==32?['ok','AES-256']:['bad','Not a valid key'];
t.className='tag keyinfo '+cls;t.textContent=txt;k.classList.toggle('badkey',cls=='bad')}
function renum(){const n=+cc.value;rowsEl.forEach((r,i)=>{r.hidden=i>=n;r.querySelector('.idx').textContent=i+1;keyInfo(r)});
$('chanN').textContent=n+' of '+MAXC+' channels';$('addCh').disabled=n>=MAXC;for(const b of document.querySelectorAll('.chan .del'))b.disabled=n<=1}
rowsEl.forEach((r,i)=>{r.querySelector('.key').oninput=()=>keyInfo(r);
r.querySelector('.gen').onclick=()=>{const b=new Uint8Array(32);crypto.getRandomValues(b);r.querySelector('.key').value=btoa(String.fromCharCode(...b));keyInfo(r);dirty()};
r.querySelector('.del').onclick=()=>{const n=+cc.value;if(n<=1)return;
for(let j=i;j<n-1;j++)for(const s of['.cname','.key'])rowsEl[j].querySelector(s).value=rowsEl[j+1].querySelector(s).value;
for(const s of['.cname','.key'])rowsEl[n-1].querySelector(s).value='';cc.value=n-1;renum();dirty()}});
$('addCh').onclick=()=>{const n=+cc.value;if(n>=MAXC)return;const r=rowsEl[n];r.querySelector('.key').value=r.querySelector('.key').value||'AQ==';cc.value=n+1;renum();r.querySelector('.cname').focus();dirty()};
renum();
// Unsaved-changes bar
const form=$('cfg');function dirty(){$('savebar').classList.add('dirty');$('saveMsg').textContent='Unsaved changes'}
form.addEventListener('input',dirty);form.addEventListener('change',dirty);
window.addEventListener('beforeunload',e=>{if($('savebar').classList.contains('dirty')&&!form.dataset.sub)e.preventDefault()});
form.addEventListener('submit',()=>form.dataset.sub=1);
// Firmware
async function fw(){try{const o=await (await fetch('/ota')).json();
$('fwCur').textContent='v'+o.current;$('fwNotes').href='https://github.com/oumike/camillia-chat-server/releases/tag/v'+o.current;$('fwNotes').hidden=false;$('fwLatest').textContent=o.latest||'not checked';
$('fwChecked').textContent=o.checkedAgoSec==null?'':'checked '+dur(o.checkedAgoSec)+' ago';
const busy=['checking','downloading','verifying','installed'].includes(o.state);
const msg=!o.supported?'Updates need the release partition layout: flash a release image over USB once.':
!o.wifi?'Join a WiFi network with internet access to check for updates.':
o.state=='checking'?'Checking for updates\u2026':o.state=='downloading'?'Downloading '+kb(o.written)+(o.total?' of '+kb(o.total):'')+'\u2026':
o.state=='verifying'?'Verifying signature\u2026':o.state=='installed'?'Installed. Restarting\u2026':
o.state=='error'?o.error:o.updateAvailable?o.latest+' is available.':o.latest?'You are on the latest release.':'';
$('fwMsg').textContent=msg;$('fwMsg').className='tag '+(o.state=='error'?'bad':o.updateAvailable||o.state=='installed'?'ok':'');
$('fwMsg').hidden=!msg;$('fwState').textContent=o.updateAvailable?'Update available':'Up to date';
$('fwCheck').disabled=busy||!o.wifi;$('fwInstall').disabled=busy||!o.updateAvailable||!o.supported||!o.wifi;
$('fwProg').hidden=o.state!='downloading'&&o.state!='verifying';$('fwProg').firstChild.style.width=(o.total?100*o.written/o.total:0)+'%';
$('pVer').lastChild.textContent='v'+o.current+(o.updateAvailable?' \u2192 '+o.latest:'');$('pVer').className='pill '+(o.updateAvailable?'warn':'');
if(o.state=='installed')setTimeout(()=>location.reload(),20000);
setTimeout(fw,busy?1000:10000)}catch(e){setTimeout(fw,5000)}}
async function fwPost(p){await fetch(p,{method:'POST'});setTimeout(fw,300)}
$('fwCheck').onclick=()=>fwPost('/ota/check');
$('fwInstall').onclick=()=>{if(confirm('Install '+$('fwLatest').textContent+'? The server restarts when it is done.'))fwPost('/ota/install')};
setInterval(()=>{if(!$('tabMonitor').hidden)load()},15000);
show(location.hash=='#monitor'||location.hash=='#messages'?'monitor':'config');status();setInterval(status,5000);fw();
)JS";

static String renderPage(const Settings &c, const char *error) {
    String h;
    h.reserve(36000);
    h += F("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>Camillia Chat Server</title><style>");
    h += kStyle;
    h += F("</style></head><body><header class=\"top\"><div class=\"wrap\"><div class=\"bar\"><div class=\"brand\">");
    h += kLogo;
    h += String("<div><b>Camillia Chat Server</b><small id=\"hdrNode\">") + esc(c.longName) + "</small></div></div>";
    h += F("<div class=\"pills\"><button type=\"button\" class=\"btn xs\" id=\"announce\" "
           "title=\"Broadcast this server's ANNOUNCE on the discovery channel\">Send announcement</button>"
           "<span class=\"pill\" id=\"pIp\"><i></i><span>&hellip;</span></span>"
           "<span class=\"pill\" id=\"pMqtt\"><i></i><span>MQTT</span></span>"
           "<span class=\"pill\" id=\"pBatt\"><i></i><span>Battery</span></span>"
           "<span class=\"pill\" id=\"pVer\"><i></i><span></span></span></div></div>"
           "<nav class=\"tabs\"><button data-tab=\"config\">Config</button>"
           "<button data-tab=\"monitor\">System Monitor</button></nav></div></header>"
           "<main class=\"wrap\"><section id=\"tabConfig\">");
    if (error && error[0]) h += String("<p class=\"err\">") + esc(error) + "</p>";
    h += F("<form method=\"post\" action=\"/save\" id=\"cfg\">");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#9673;</span>Identity</h2>"
           "<p class=\"sub\">How this server appears to other nodes on the mesh.</p><div class=\"grid\">");
    h += textInput("Node name", "longName", c.longName, 24);
    h += textInput("Short name", "shortName", c.shortName, 4, "text", "Up to 4 characters");
    h += F("</div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#8767;</span>Radio</h2>"
           "<p class=\"sub\">LoRa region and modem preset. These must match the mesh you serve.</p><div class=\"grid\">");
    String sel = "<select name=\"region\">";
    for (uint8_t i = 0; i < kRegionCount; i++) {
        sel += String("<option") + (strcmp(kRegions[i].code, c.region) == 0 ? " selected" : "") + ">" +
               kRegions[i].code + "</option>";
    }
    h += field("Region", sel + "</select>");
    sel = "<select name=\"preset\">";
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (!presetUsableOnThisRadio(i)) continue;
        sel += String("<option value=\"") + i + "\"" + (i == c.modemPreset ? " selected" : "") + ">" +
               kPresets[i].name + "</option>";
    }
    h += field("Preset", sel + "</select>");
    h += numInput("Frequency slot", "slot", c.freqSlot, 0, 255, "0 = automatic, from the preset name");
    h += F("</div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">#</span>Channels</h2>"
           "<p class=\"sub\">Channels the server stores and replays. Names and keys must match the "
           "channels on your nodes. Keys are base64: <span class=\"mono\">AQ==</span> is the default "
           "Meshtastic key, empty means no encryption.</p><div class=\"chans\">");
    h += String("<input type=\"hidden\" name=\"chanCount\" value=\"") + c.chanCount + "\">";
    for (int i = 0; i < SETTINGS_MAX_CHANNELS; i++) {
        char key[48];
        formatKeyBase64(c.ch[i].key, c.ch[i].keyLen, key, sizeof key);
        h += String("<div class=\"chan\"") + (i >= c.chanCount ? " hidden" : "") + "><div class=\"idx\">" + (i + 1) +
             "</div><label class=\"field\"><span>Name</span><input class=\"cname\" name=\"cn" + i +
             "\" maxlength=\"11\" value=\"" + esc(c.ch[i].name) + "\" autocomplete=\"off\"></label>"
             "<label class=\"field k\"><span>Key <em class=\"tag keyinfo\"></em></span><div class=\"keybox\">"
             "<input class=\"key mono\" name=\"ck" + i + "\" maxlength=\"47\" value=\"" + esc(key) +
             "\" autocomplete=\"off\" spellcheck=\"false\"><button type=\"button\" class=\"icon gen\" "
             "title=\"Generate a random AES-256 key\" aria-label=\"Generate key\"><svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\"><path d=\"M21 12a9 9 0 1 1-2.6-6.4\"/><path d=\"M21 3v6h-6\"/></svg></button></div></label>"
             "<button type=\"button\" class=\"icon del\" title=\"Remove channel\">&#10005;</button></div>";
    }
    h += F("</div><div class=\"chanfoot\"><span id=\"chanN\"></span>"
           "<button type=\"button\" class=\"btn sm\" id=\"addCh\">+ Add channel</button></div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#9636;</span>Storage</h2>"
           "<p class=\"sub\">How many messages each channel keeps for nodes to catch up on. When a "
           "channel is full, its oldest message is dropped. Lowering this drops the oldest messages "
           "beyond the new limit on restart.</p><div class=\"grid\">");
    h += numInput("Messages per channel", "msgCap", c.msgsPerChannel, CS_MSGS_PER_CHANNEL_MIN,
                  CS_MSGS_PER_CHANNEL_MAX, "10-1000; about 224 bytes of PSRAM each, per channel");
    h += F("</div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#8634;</span>Replies</h2>"
           "<p class=\"sub\">How stored messages are sent back to nodes catching up.</p><div class=\"grid\">");
    h += numInput("Batch size", "batch", c.batchSize, 1, 50, "Messages per request (1-50)");
    h += numInput("Gap between packets", "gap", c.packetGapMs, 500, 60000, "Milliseconds (500-60000)");
    h += numInput("Max hops", "hops", c.maxHops, 0, 7);
    h += F("</div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#8645;</span>MQTT</h2>"
           "<p class=\"sub\">Receive only: messages heard by the MQTT broker are stored as if heard on air.</p>");
    h += toggle("Enabled", "mqttOn", c.mqttEnabled, "Needs a home WiFi network with internet access");
    h += F("<div class=\"grid\">");
    h += textInput("Broker", "mqttHost", c.mqttHost, 63);
    h += numInput("Port", "mqttPort", c.mqttPort, 1, 65535);
    h += textInput("Username", "mqttUser", c.mqttUser, 31);
    h += textInput("Password", "mqttPass", c.mqttPass, 63, "password");
    h += textInput("Topic root", "mqttRoot", c.mqttRoot, 47);
    h += F("</div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#9788;</span>Display</h2>"
           "<p class=\"sub\">Used by the colour touchscreen builds (V4 expansion, Wio Tracker L2).</p><div class=\"grid\">");
    h += numInput("Brightness", "dispBright", c.displayBrightness, 10, 255, "10-255");
    h += numInput("Dim after", "dispDim", c.displayDimAfterSec, 0, 3600, "Seconds on battery; 0 = never");
    h += numInput("Dimmed brightness", "dispDimLvl", c.displayDimLevel, 0, 254, "0-254, below brightness; 0 = screen off");
    h += numInput("Page change time", "dispPage", c.displayPageSec, 10, 300, "Seconds (10-300)");
    h += F("</div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#8984;</span>WiFi</h2>"
           "<p class=\"sub\">Join a home network for MQTT, the clock and updates. While joined, the "
           "<b>camillia-cs-&hellip;</b> setup access point is turned off; it comes back if the network "
           "is lost for 30 seconds.</p><div class=\"grid\">");
    h += textInput("Home WiFi SSID", "staSsid", c.staSsid, 32, "text", "Optional");
    h += textInput("Home WiFi password", "staPass", c.staPass, 63, "password");
    h += textInput("Time zone", "tz", c.tz, 47, "text", "POSIX TZ string, for the screen clock");
    h += F("</div></div>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#8679;</span>Firmware</h2>"
           "<p class=\"sub\">Signed releases from the camillia update server. Each image is checked "
           "against the built-in key before it is installed.</p><div class=\"fw\">"
           "<div class=\"kv\"><span>Installed</span><b id=\"fwCur\">&hellip;</b>"
           "<small><a id=\"fwNotes\" target=\"_blank\" rel=\"noopener\" hidden>Release notes</a></small></div>"
           "<div class=\"kv\"><span>Latest release</span><b id=\"fwLatest\">&hellip;</b><small id=\"fwChecked\"></small></div>"
           "<div class=\"kv\"><span>Status</span><b id=\"fwState\">&hellip;</b></div></div>"
           "<div class=\"prog\" id=\"fwProg\" hidden><i></i></div>"
           "<div class=\"actions\"><button type=\"button\" class=\"btn\" id=\"fwCheck\">Check for updates</button>"
           "<button type=\"button\" class=\"btn primary\" id=\"fwInstall\">Install update</button>"
           "<span class=\"tag\" id=\"fwMsg\" hidden></span></div>");
    h += toggle("Install updates automatically", "otaAuto", c.otaAutoInstall,
                "Checks after joining WiFi and then daily; installs a newer stable release and restarts");
    h += F("</div></form>");

    h += F("<div class=\"card\"><h2><span class=\"ico\">&#8681;</span>Backup</h2>"
           "<p class=\"sub\">The exported file includes channel keys and passwords.</p>"
           "<div class=\"actions\"><a class=\"btn\" href=\"/export.yaml\">Export config (YAML)</a></div>"
           "<form method=\"post\" action=\"/import\">"
           "<label class=\"field\"><span>Import from file</span><input type=\"file\" "
           "accept=\".yaml,.yml,text/yaml,text/plain\" "
           "onchange=\"const f=this.files[0];if(f)f.text().then(t=>this.form.yaml.value=t)\"></label>"
           "<label class=\"field\"><span>Or paste YAML</span><textarea name=\"yaml\" rows=\"6\" class=\"mono\"></textarea></label>"
           "<div class=\"actions\"><button type=\"submit\" class=\"btn\">Import and restart</button></div></form></div>"
           "</section>");

    h += F("<section id=\"tabMonitor\" hidden>"
           "<div class=\"card\"><h2><span class=\"ico\">&#9881;</span>Overview</h2><p class=\"sub\">Live, every 5 seconds.</p>"
           "<div class=\"stats\" id=\"ov\"></div>"
           "<details><summary>Raw status JSON</summary><pre id=\"raw\">loading&hellip;</pre></details></div>"
           "<div class=\"card\"><h2><span class=\"ico\">&#9636;</span>Storage</h2><p class=\"sub\">Messages kept for nodes to catch up on.</p>"
           "<div class=\"stats\" id=\"stats\"></div></div>"
           "<div class=\"card\"><h2><span class=\"ico\">&#9993;</span>Messages</h2>"
           "<div class=\"toolbar\"><label class=\"field\"><span>Channel</span><select id=\"ch\">");
    for (int i = 0; i < c.chanCount; i++) {
        h += String("<option value=\"") + i + "\">" + esc(c.ch[i].name) + "</option>";
    }
    h += F("</select></label><button id=\"refresh\" type=\"button\" class=\"btn\">Refresh</button>"
           "<button id=\"clear1\" type=\"button\" class=\"btn danger\">Clear channel</button>"
           "<button id=\"clearAll\" type=\"button\" class=\"btn danger\">Clear all</button></div>"
           "<p id=\"count\" class=\"sub\"></p><div class=\"msgs\" id=\"list\"></div></div></section></main>"
           "<div class=\"savebar\" id=\"savebar\"><div class=\"wrap\"><span id=\"saveMsg\">"
           "Saving restarts the server.</span><button type=\"submit\" form=\"cfg\" class=\"btn primary\">"
           "Save and restart</button></div></div><script>");
    h += kScript;
    h += F("</script></body></html>");
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
    n.msgsPerChannel = (uint16_t)constrain(s_server.arg("msgCap").toInt(), 0, 65535);
    n.mqttEnabled = s_server.hasArg("mqttOn");
    copyArg("mqttHost", n.mqttHost, sizeof n.mqttHost);
    n.mqttPort = (uint16_t)s_server.arg("mqttPort").toInt();
    copyArg("mqttUser", n.mqttUser, sizeof n.mqttUser);
    copyArg("mqttPass", n.mqttPass, sizeof n.mqttPass);
    copyArg("mqttRoot", n.mqttRoot, sizeof n.mqttRoot);
    copyArg("staSsid", n.staSsid, sizeof n.staSsid);
    copyArg("staPass", n.staPass, sizeof n.staPass);
    copyArg("tz", n.tz, sizeof n.tz);
    n.displayBrightness = (uint8_t)constrain(s_server.arg("dispBright").toInt(), 0, 255);
    n.displayDimAfterSec = (uint16_t)constrain(s_server.arg("dispDim").toInt(), 0, 65535);
    n.displayDimLevel = (uint8_t)constrain(s_server.arg("dispDimLvl").toInt(), 0, 255);
    n.displayPageSec = (uint16_t)constrain(s_server.arg("dispPage").toInt(), 0, 65535);
    n.otaAutoInstall = s_server.hasArg("otaAuto");

    if (!err[0] && settingsValidateDevice(n, err, sizeof err)) persistFits(n, err, sizeof err);
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
    if (!settingsFromYaml(s_server.arg("yaml").c_str(), n, kPresetMap, err, sizeof err) ||
        !settingsValidateDevice(n, err, sizeof err) || !persistFits(n, err, sizeof err)) {
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
                    "content=\"width=device-width,initial-scale=1\"><title>Restarting</title>"
                    "<style>body{margin:0;min-height:100vh;display:grid;place-items:center;"
                    "font:16px system-ui,sans-serif;background:#f4f5f8;color:#1b1f24}"
                    "@media(prefers-color-scheme:dark){body{background:#0d1015;color:#e8ecf1}}"
                    "div{text-align:center}i{display:block;width:36px;height:36px;margin:0 auto 14px;"
                    "border:4px solid #d8507033;border-top-color:#d85070;border-radius:50%;"
                    "animation:r 1s linear infinite}@keyframes r{to{transform:rotate(360deg)}}"
                    "small{opacity:.7}</style><div><i></i><b>Saved. Restarting&hellip;</b><br>"
                    "<small>If the WiFi settings changed, reconnect to the new network.</small></div>"
                    "<script>setTimeout(()=>location='/',8000)</script>"));
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
    s_server.on("/ota", HTTP_GET, [] { s_server.send(200, "application/json", otaStatusJson()); });
    s_server.on("/ota/check", HTTP_POST, [] {
        s_server.send(otaRequestCheck() ? 202 : 409, "application/json", otaStatusJson());
    });
    s_server.on("/ota/install", HTTP_POST, [] {
        s_server.send(otaRequestInstall() ? 202 : 409, "application/json", otaStatusJson());
    });
    s_server.on("/announce", HTTP_POST, [] {
        bool ok = s_announce && s_announce();
        s_server.send(ok ? 202 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    s_server.on("/status", HTTP_GET, [] {
        s_server.send(200, "application/json", s_status ? s_status() : String("{}"));
    });
    s_server.begin();
}

void webSetAnnounce(std::function<bool()> announce) { s_announce = announce; }

void webLoop() { s_server.handleClient(); }
