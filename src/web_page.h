// =============================================================================
//  Veebiliidese lehed (HTML + CSS + JS) – talletatud flash-mälus (PROGMEM)
//   INDEX_HTML    – vaade: ainult kaamerapilt + nupud
//   SETTINGS_HTML – olek, WiFi/LTE seaded, parool, taaskäivitus
//   LOGIN_HTML    – sisselogimine
//   COMMON_CSS    – ühine stiil (/style.css)
// =============================================================================
#pragma once
#include <Arduino.h>

static const char COMMON_CSS[] PROGMEM = R"CSS(
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7385;--line:#e3e6ec;
 --ok:#1f9d55;--warn:#d98b00;--bad:#d64545;--acc:#2f6fed}
@media (prefers-color-scheme:dark){:root{--bg:#12151b;--card:#1b2029;--fg:#e8ebf1;
 --mut:#8b93a5;--line:#2a303b;--acc:#5b8dff}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
button,.btn{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:10px;padding:9px 14px;cursor:pointer;text-decoration:none;display:inline-flex;align-items:center;gap:6px}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}
button.dng{border-color:var(--bad);color:var(--bad)}
button:disabled{opacity:.5;cursor:default}
input[type=text],input[type=password]{font:inherit;font-size:15px;padding:9px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);width:100%}
.toast{position:fixed;bottom:84px;left:50%;transform:translateX(-50%);background:var(--fg);color:var(--bg);padding:8px 16px;border-radius:8px;opacity:0;transition:opacity .3s;pointer-events:none;z-index:9}
.toast.show{opacity:1}
)CSS";

// -----------------------------------------------------------------------------
//  Vaade – ainult pilt ja nupud
// -----------------------------------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="et"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>SimCam</title><link rel="stylesheet" href="/style.css">
<style>
html,body{height:100%;background:#000}
body{display:flex;flex-direction:column;color:#fff}
.stage{flex:1;min-height:0;display:flex;align-items:center;justify-content:center;position:relative;overflow:hidden}
.stage img{max-width:100%;max-height:100%;object-fit:contain;display:block}
.msg{position:absolute;color:#9aa;font-size:15px;text-align:center;padding:16px}
.gear{position:absolute;top:10px;right:10px;background:rgba(0,0,0,.45);border:1px solid rgba(255,255,255,.2);color:#fff;border-radius:50%;width:42px;height:42px;display:flex;align-items:center;justify-content:center;font-size:20px;text-decoration:none}
.bar{display:flex;justify-content:center;gap:8px;flex-wrap:wrap;padding:10px 10px calc(10px + env(safe-area-inset-bottom));background:#0d0f14;border-top:1px solid #222}
.bar button{background:#1b2029;border-color:#2a303b;color:#e8ebf1;min-width:52px;justify-content:center;padding:10px 14px;font-size:15px}
.bar button.on{background:var(--acc);border-color:var(--acc);color:#fff}
.bar .lbl{font-size:13px}
.info{display:flex;justify-content:center;align-items:center;gap:10px;flex-wrap:wrap;padding:6px 10px;background:#0d0f14;color:#aab2c0;font-size:13px;font-variant-numeric:tabular-nums;border-top:1px solid #222;min-height:30px}
.info b{color:#e8ebf1;font-weight:600}
.info .warn{color:#f0b429}
.rot{display:inline-flex;border:1px solid #2a303b;border-radius:10px;overflow:hidden}
.bar .rot button{border:0;border-radius:0;min-width:48px;padding:10px 10px;border-right:1px solid #2a303b}
.bar .rot button:last-child{border-right:0}
@media (max-width:420px){.bar .lbl{display:none}.bar button{font-size:18px;padding:10px 11px}.bar .rot button{font-size:14px;min-width:42px}}
</style></head><body>
<div class="stage">
  <img id="view" alt="">
  <span class="msg" id="msg">Ühendan…</span>
  <a class="gear" href="/settings" title="Seaded">⚙</a>
</div>
<div class="info" id="info"><span id="iStat">Ühendan…</span><span class="warn" id="iWarn" style="display:none">⚠ 90°/270° pööre vähendab kaadrisagedust (~3–4 fps)</span></div>
<div class="bar">
  <button id="bPlay" title="Peata / jätka vaade">⏸<span class="lbl">Peata</span></button>
  <span class="rot" title="Pildi pööre"><button data-r="0">0°</button><button data-r="90">90°</button><button data-r="180">180°</button><button data-r="270">270°</button></span>
  <button id="bFocus" title="Autofookus">◎<span class="lbl">Fookus</span></button>
  <button id="bSnap" title="Hetktõmmis">📷<span class="lbl">Hetktõmmis</span></button>
</div>
<div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);let playing=false,rot=0;
const vid=Math.random().toString(36).slice(2,12);
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),2200)}
function setPlay(p){playing=p;const v=$('view');
 if(p){v.src='/stream?id='+vid+'&t='+Date.now();$('bPlay').innerHTML='⏸<span class="lbl">Peata</span>';$('bPlay').classList.remove('on')}
 else{v.removeAttribute('src');$('msg').textContent='Vaade peatatud';$('bPlay').innerHTML='▶<span class="lbl">Jätka</span>';$('bPlay').classList.add('on')}}
$('bPlay').onclick=()=>setPlay(!playing);
$('view').onload=()=>{$('msg').textContent=''};
$('view').onerror=()=>{if(playing){$('msg').textContent='Voog katkes – ühendan uuesti…';setTimeout(()=>playing&&setPlay(true),3000)}};
async function api(u,o){const r=await fetch(u,o);if(r.status==401){location.href='/login';throw 0}return r}
function showRot(){document.querySelectorAll('.rot button').forEach(b=>b.classList.toggle('on',+b.dataset.r===rot));
 $('iWarn').style.display=(rot==90||rot==270)?'':'none'}
async function rotate(n){if(n===rot)return;
 try{const r=await api('/api/cam?var=rotate&val='+n);if(r.ok){rot=n;showRot();toast('Pööre '+n+'°');if(playing)setPlay(true)}else toast('Pööramine ebaõnnestus')}catch(e){}}
document.querySelectorAll('.rot button').forEach(b=>b.onclick=()=>rotate(+b.dataset.r));
async function stats(){
 if(!playing){$('iStat').textContent='Vaade peatatud'}
 else try{const d=await (await api('/api/view?id='+vid,{cache:'no-store'})).json();
  if(d.rotate!==rot){rot=d.rotate;showRot()}
  if(d.fps<0)$('iStat').textContent='Ühendan…';
  else $('iStat').innerHTML=`<b>${d.fps.toFixed(1)} fps</b> · <b>${d.kBps.toFixed(0)} kB/s</b> (${(d.kBps*8/1024).toFixed(2)} Mbit/s) · kaader ${d.frame_kb.toFixed(1)} kB`;
 }catch(e){}
 setTimeout(stats,2000)}
$('bFocus').onclick=async()=>{const b=$('bFocus');b.disabled=true;
 try{const r=await api('/api/focus',{method:'POST'});toast(r.ok?'Autofookus käivitatud':'Autofookus pole saadaval')}catch(e){}
 setTimeout(()=>b.disabled=false,1500)};
$('bSnap').onclick=()=>{const a=document.createElement('a');const t=new Date().toISOString().slice(0,19).replace(/[:T]/g,'-');
 a.href='/capture?'+Date.now();a.download='simcam-'+t+'.jpg';document.body.appendChild(a);a.click();a.remove();toast('Hetktõmmis salvestatud')};
api('/api/cam').then(r=>r.json()).then(c=>{rot=c.rotate||0;showRot()}).catch(()=>{});
setPlay(true);stats();
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
//  Seaded ja olek
// -----------------------------------------------------------------------------
static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="et"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SimCam – seaded</title><link rel="stylesheet" href="/style.css">
<style>
header{display:flex;align-items:center;gap:12px;padding:10px 16px;border-bottom:1px solid var(--line);background:var(--card);position:sticky;top:0;z-index:2}
header h1{font-size:18px;margin:0;flex:1}
.dot{width:9px;height:9px;border-radius:50%;background:var(--mut);display:inline-block;margin-right:6px;vertical-align:1px}
.dot.ok{background:var(--ok)}.dot.warn{background:var(--warn)}.dot.bad{background:var(--bad)}
main{max-width:1180px;margin:0 auto;padding:16px}
h3{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);margin:18px 4px 10px}
h3:first-child{margin-top:4px}
.cols{columns:3 330px;column-gap:16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px;align-items:start;margin-bottom:16px}
.grid>.card{margin:0}
.card{break-inside:avoid;display:inline-block;width:100%;margin:0 0 16px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;min-width:0}
.card h2{font-size:15px;margin:0 0 10px;display:flex;align-items:center;gap:8px}
.row{display:flex;justify-content:space-between;align-items:baseline;gap:12px;padding:6px 0;border-bottom:1px solid var(--line)}
.row:last-child{border:0}
.row>span:first-child{color:var(--mut);flex:none}
.row>span:last-child{text-align:right;font-variant-numeric:tabular-nums;min-width:0;overflow-wrap:anywhere}
.bars{display:inline-flex;gap:2px;align-items:flex-end;height:13px;margin-right:6px;vertical-align:-1px}
.bars i{width:4px;background:var(--line);border-radius:1px}.bars i.on{background:var(--ok)}
code{background:var(--bg);padding:3px 6px;border-radius:6px;font-size:13px;overflow-wrap:anywhere}
form{display:grid;gap:10px}
label{display:grid;gap:4px;font-size:13px;color:var(--mut)}
label input{font-size:15px}
label.chk{display:flex;align-items:center;gap:10px;color:var(--fg);font-size:15px}
label.chk input{width:18px;height:18px;margin:0;accent-color:var(--acc)}
.inl{display:flex;gap:8px}.inl input{flex:1;min-width:0}
.note{font-size:12.5px;color:var(--mut);line-height:1.4}
.warn{font-size:13px;color:var(--warn);background:color-mix(in srgb,var(--warn) 12%,transparent);border-radius:8px;padding:8px 10px}
.btns{display:flex;flex-wrap:wrap;gap:8px}
.bar{height:8px;background:var(--line);border-radius:4px;overflow:hidden}.bar>div{height:100%;width:0;background:var(--acc);transition:width .4s}
.url{display:flex;gap:8px;align-items:center;margin:4px 0 8px}.url code{flex:1}
hr{border:0;border-top:1px solid var(--line);margin:4px 0}
.ipbox{background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:9px 12px;font-size:14px}
.ipbox b{font-variant-numeric:tabular-nums}
</style></head><body>
<header><a class="btn" href="/">← Vaade</a><h1>Seaded</h1><span id="hdr"><span class="dot"></span>…</span></header>
<main>
<h3>Olek</h3>
<div class="grid">
 <section class="card"><h2>📷 Kaamera</h2>
  <div class="row"><span>Sensor / pilt</span><span id="c_res">-</span></div>
  <div class="row"><span>FPS / kaader</span><span id="c_fps">-</span></div>
  <div class="row"><span>Autofookus</span><span id="c_af">-</span></div>
  <div class="row"><span>Pööre</span><span id="c_rot">-</span></div>
  <div class="row"><span>Vaatajad RTSP / veeb</span><span id="c_cli">-</span></div>
 </section>
 <section class="card"><h2>📶 Võrk</h2>
  <div class="row"><span>WiFi</span><span id="w_sta">-</span></div>
  <div class="row"><span>WiFi IP / levi</span><span id="w_ip">-</span></div>
  <div class="row"><span>Hotspot</span><span id="w_ap">-</span></div>
  <div class="row"><span>LTE</span><span id="m_state">-</span></div>
  <div class="row"><span>LTE operaator / levi</span><span id="m_op">-</span></div>
  <div class="row"><span>LTE IP</span><span id="m_ip">-</span></div>
  <div class="row"><span>LTE viga</span><span id="m_err">-</span></div>
 </section>
 <section class="card"><h2>⚙ Süsteem</h2>
  <div class="row"><span>Tööaeg</span><span id="s_up">-</span></div>
  <div class="row"><span>RAM vaba</span><span id="s_ram">-</span></div>
  <div class="row"><span>PSRAM vaba</span><span id="s_ps">-</span></div>
  <div class="row"><span>Temperatuur</span><span id="s_t">-</span></div>
  <div class="row"><span>Püsivara</span><span id="s_fw">-</span></div>
  <div class="btns" style="margin-top:10px"><button class="dng" id="bReboot">Taaskäivita</button><a class="btn" href="/logout">Logi välja</a></div>
 </section>
</div>

<h3>Seaded</h3>
<div class="cols">
 <section class="card"><h2>WiFi võrk</h2>
  <form id="fWifi" autocomplete="off">
   <label class="chk"><input type="checkbox" id="f_sta_en"> Ühendu WiFi võrku</label>
   <label>Võrgu nimi (SSID)<span class="inl"><input type="text" id="f_sta_ssid" list="ssids" maxlength="32"><button type="button" id="bScan">Otsi</button></span></label>
   <datalist id="ssids"></datalist>
   <label>Parool<input type="password" id="f_sta_pass" maxlength="64" placeholder="(muutmata)"></label>
   <div class="btns"><button class="pri" type="submit">Salvesta</button></div>
  </form>
 </section>

 <section class="card"><h2>Hotspot</h2>
  <form id="fAp" autocomplete="off">
   <label class="chk"><input type="checkbox" id="f_ap_en"> Hotspot sees</label>
   <label>Nimi<input type="text" id="f_ap_ssid" maxlength="32"></label>
   <label>Parool (min 8 märki)<input type="password" id="f_ap_pass" maxlength="64" placeholder="(muutmata)"></label>
   <div class="note">Kui WiFi ja LTE on mõlemad väljas, jääb hotspot alati sisse. Kui võrguühendus kaob 2 minutiks, lülitub hotspot ajutiselt ise sisse.</div>
   <div class="btns"><button class="pri" type="submit">Salvesta</button></div>
  </form>
 </section>

 <section class="card"><h2>RTSP ja parool</h2>
  <div class="note">RTSP aadress (VLC: Meedia → Ava võrguvoog):</div>
  <div class="url"><code id="rtsp">-</code><button type="button" id="bCopy" title="Kopeeri">⧉</button></div>
  <form id="fRtsp">
   <label class="chk"><input type="checkbox" id="f_rtsp_auth"> RTSP nõuab parooli</label>
   <div class="btns"><button class="pri" type="submit">Salvesta</button></div>
  </form>
  <hr>
  <div class="warn" id="defpass" style="display:none">⚠ Kasutusel on vaikeparool – muuda see!</div>
  <form id="fPw" autocomplete="off">
   <label>Uus parool (veeb + RTSP, min 4 märki)<input type="password" id="p1" maxlength="64" autocomplete="new-password"></label>
   <label>Korda uut parooli<input type="password" id="p2" maxlength="64" autocomplete="new-password"></label>
   <div class="btns"><button class="pri" type="submit">Muuda parool</button></div>
  </form>
 </section>

 <section class="card"><h2>Mobiilivõrk (LTE)</h2>
  <form id="fLte" autocomplete="off">
   <label class="chk"><input type="checkbox" id="f_lte_en"> LTE modem sees</label>
   <label>APN<input type="text" id="f_apn" maxlength="63" placeholder="nt operaatori staatilise IP APN"></label>
   <label>SIM PIN, mida seade kasutab<input type="password" id="f_pin" maxlength="8" inputmode="numeric" placeholder="(muutmata)"></label>
   <label class="chk"><input type="checkbox" id="f_pin_clear"> SIM-il pole PIN-i</label>
   <div class="ipbox" id="lteIp">Mobiilivõrgu IP: –</div>
   <div class="note">Ainult UART-modemiga (LilyGO T-PCIe SIM7600). MikroTik R11e-LTE puhul hoia väljas! Muudatused rakenduvad pärast taaskäivitust.</div>
   <div class="btns"><button class="pri" type="submit">Salvesta</button></div>
  </form>
 </section>

 <section class="card"><h2>SIM-kaardi PIN-i muutmine</h2>
  <form id="fSim" autocomplete="off">
   <div class="note">Muudab PIN-koodi SIM-kaardil endal (modem peab olema võrku ühendatud). Uus PIN salvestatakse ka seadmesse.</div>
   <label>Praegune PIN<input type="password" id="s_old" maxlength="8" inputmode="numeric"></label>
   <label>Uus PIN (4–8 numbrit)<input type="password" id="s_new" maxlength="8" inputmode="numeric"></label>
   <label>Korda uut PIN-i<input type="password" id="s_new2" maxlength="8" inputmode="numeric"></label>
   <div class="warn">3 valet katset lukustab SIM-i (vaja PUK-koodi).</div>
   <div class="btns"><button class="pri" type="submit">Muuda SIM PIN</button></div>
  </form>
 </section>

 <section class="card"><h2>Püsivara</h2>
  <div class="row"><span>Praegune</span><span id="o_cur">-</span></div>
  <div class="row"><span>Viimane GitHubis</span><span id="o_lat">-</span></div>
  <div class="row"><span>Olek</span><span id="o_st">-</span></div>
  <div class="bar" id="o_barw" style="display:none;margin:8px 0"><div id="o_bar"></div></div>
  <div class="btns" style="margin-top:10px"><button id="bOtaChk">Kontrolli uuendusi</button><button class="pri" id="bOtaUpd" disabled>Uuenda</button></div>
 </section>
</div>
</main>
<div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),3000)}
async function api(u,o){const r=await fetch(u,o);if(r.status==401){location.href='/login';throw 0}return r}
const form=o=>({method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(o)});
function dur(s){s=Math.floor(s);const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
 return (d?d+'p ':'')+String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(s%60).padStart(2,'0')}
function kb(b){return b>1048576?(b/1048576).toFixed(2)+' MB':(b/1024).toFixed(0)+' kB'}
function bars(n){let h='<span class="bars">';for(let i=0;i<4;i++)h+=`<i class="${i<n?'on':''}" style="height:${4+i*3}px"></i>`;return h+'</span>'}
let rtspAuth=true;
async function loadCfg(){try{const c=await (await api('/api/config',{cache:'no-store'})).json();
 $('f_sta_en').checked=c.sta_en;$('f_sta_ssid').value=c.sta_ssid;$('f_sta_pass').placeholder=c.sta_has_pass?'(muutmata)':'(avatud võrk)';
 $('f_ap_en').checked=c.ap_en;$('f_ap_ssid').value=c.ap_ssid;
 $('f_lte_en').checked=c.lte_en;$('f_apn').value=c.apn;
 $('f_pin').placeholder=c.has_pin?'(salvestatud)':'(puudub)';$('f_pin_clear').checked=false;
 $('f_rtsp_auth').checked=c.rtsp_auth;rtspAuth=c.rtsp_auth;showRtsp();
 $('defpass').style.display=c.default_pass?'block':'none'}catch(e){}}
function showRtsp(){$('rtsp').textContent=rtspAuth?`rtsp://admin:<parool>@${location.hostname}:554/live`:`rtsp://${location.hostname}:554/live`}
$('bCopy').onclick=()=>{const t=$('rtsp').textContent;(navigator.clipboard?navigator.clipboard.writeText(t):Promise.reject()).then(()=>toast('Kopeeritud'),()=>toast(t))};
async function save(fields,msg){
 try{const d=await (await api('/api/config',form(fields))).json();
  if(!d.ok){toast(d.error||'Viga');return false}
  let t=msg||'Salvestatud';if(d.ap_forced)t+=' – hotspot jääb sisse (WiFi ja LTE on väljas)';if(d.reboot)t+=' – rakendub pärast taaskäivitust';
  toast(t);loadCfg();return true}catch(e){if(e!==0)toast('Salvestatud – ühendus võis hetkeks katkeda');return false}}
$('bScan').onclick=async()=>{const b=$('bScan');b.disabled=true;b.textContent='…';
 try{const l=await (await api('/api/scan',{cache:'no-store'})).json();l.sort((a,b)=>b.rssi-a.rssi);
  $('ssids').innerHTML=l.map(n=>`<option value="${n.ssid.replace(/"/g,'&quot;')}">${n.rssi} dBm${n.enc?' 🔒':''}</option>`).join('');
  toast(l.length+' võrku leitud');$('f_sta_ssid').focus()}catch(e){toast('Otsing ebaõnnestus')}
 b.disabled=false;b.textContent='Otsi'};
$('fWifi').onsubmit=e=>{e.preventDefault();save({sta_en:$('f_sta_en').checked?1:0,sta_ssid:$('f_sta_ssid').value,sta_pass:$('f_sta_pass').value},'WiFi salvestatud').then(o=>{if(o)$('f_sta_pass').value=''})};
$('fAp').onsubmit=e=>{e.preventDefault();const p=$('f_ap_pass').value;if(p&&p.length<8){toast('Parool peab olema vähemalt 8 märki');return}
 save({ap_en:$('f_ap_en').checked?1:0,ap_ssid:$('f_ap_ssid').value,ap_pass:p},'Hotspot salvestatud').then(o=>{if(o)$('f_ap_pass').value=''})};
$('fRtsp').onsubmit=e=>{e.preventDefault();save({rtsp_auth:$('f_rtsp_auth').checked?1:0},'RTSP seade salvestatud')};
$('fLte').onsubmit=e=>{e.preventDefault();const pin=$('f_pin').value.trim();
 if(pin&&!/^[0-9]{4,8}$/.test(pin)){toast('PIN peab olema 4–8 numbrit');return}
 save({lte_en:$('f_lte_en').checked?1:0,apn:$('f_apn').value.trim(),sim_pin:pin,sim_pin_clear:$('f_pin_clear').checked?1:0},'LTE seaded salvestatud').then(o=>{if(o)$('f_pin').value=''})};
$('fPw').onsubmit=async e=>{e.preventDefault();const a=$('p1').value,b=$('p2').value;
 if(a.length<4){toast('Parool peab olema vähemalt 4 märki');return}if(a!==b){toast('Paroolid ei kattu');return}
 try{const r=await api('/api/password',form({pass:a}));toast(r.ok?'Parool muudetud':'Parooli muutmine ebaõnnestus');if(r.ok){$('p1').value='';$('p2').value='';loadCfg()}}catch(e){}};
$('fSim').onsubmit=async e=>{e.preventDefault();const o=$('s_old').value.trim(),n=$('s_new').value.trim();
 if(!/^[0-9]{4,8}$/.test(o)||!/^[0-9]{4,8}$/.test(n)){toast('PIN peab olema 4–8 numbrit');return}
 if(n!==$('s_new2').value.trim()){toast('Uued PIN-id ei kattu');return}
 if(!confirm('Muuta SIM-kaardi PIN?'))return;toast('Muudan SIM PIN-i…');
 try{const d=await (await api('/api/simpin',form({old:o,new:n}))).json();
  toast(d.ok?'SIM PIN muudetud':('Ebaõnnestus: '+(d.error||'')));if(d.ok){['s_old','s_new','s_new2'].forEach(i=>$(i).value='')}}catch(e){}};
$('bReboot').onclick=async()=>{if(!confirm('Kas taaskäivitada seade?'))return;try{await api('/api/reboot',{method:'POST'})}catch(e){}toast('Taaskäivitan…')};
const OST={idle:'-',checking:'Kontrollin…',uptodate:'Ajakohane ✓',available:'Uuendus saadaval!',updating:'Uuendan…',done:'Paigaldatud – taaskäivitub',error:'Viga'};
async function otaPoll(){try{const o=await (await api('/api/ota',{cache:'no-store'})).json();
 $('o_cur').textContent=o.current;$('o_lat').textContent=o.latest||'-';
 let st=OST[o.state]||o.state;if(o.state=='error')st+=': '+o.error;if(o.state=='updating')st+=' '+o.progress+'%';
 if(o.checked_ago&&(o.state=='uptodate'||o.state=='available'))st+=` (${Math.round(o.checked_ago/60)} min tagasi)`;
 $('o_st').textContent=st;$('bOtaUpd').disabled=o.state!='available';$('bOtaChk').disabled=o.state=='checking'||o.state=='updating';
 $('o_barw').style.display=(o.state=='updating'||o.state=='done')?'block':'none';$('o_bar').style.width=o.progress+'%';
 setTimeout(otaPoll,(o.state=='updating'||o.state=='checking')?1000:5000)}catch(e){setTimeout(otaPoll,5000)}}
$('bOtaChk').onclick=async()=>{try{await api('/api/ota/check',{method:'POST'});$('o_st').textContent='Kontrollin…'}catch(e){}};
$('bOtaUpd').onclick=async()=>{if(!confirm('Paigaldada uus püsivara? Seade taaskäivitub. Kui uus versioon ei tööta, taastatakse eelmine automaatselt.'))return;
 try{const r=await api('/api/ota/update',{method:'POST'});toast(r.ok?'Uuendus alustatud':'Uuendust pole saadaval')}catch(e){}};
async function poll(){
 try{const d=await (await api('/api/status',{cache:'no-store'})).json();const m=d.lte,c=d.cam,s=d.sys,w=d.wifi;
  const on=[];if(w.sta_ok)on.push('WiFi');if(w.ap_clients)on.push('Hotspot');if(m.connected)on.push('LTE');
  $('hdr').innerHTML=`<span class="dot ${on.length?'ok':'warn'}"></span>${on.length?on.join(' + '):'Võrguühendus puudub'}`;
  const wq=q=>q>=-55?4:q>=-65?3:q>=-75?2:q>=-85?1:0,cq=q=>q>=99?0:q>=20?4:q>=15?3:q>=10?2:q>=2?1:0;
  $('w_sta').innerHTML=w.sta_en?`<span class="dot ${w.sta_ok?'ok':'warn'}"></span>${w.sta_ssid}${w.sta_ok?'':' (ühendun…)'}`:'väljas';
  $('w_ip').innerHTML=w.sta_ok?`${w.sta_ip} · ${bars(wq(w.sta_rssi))}${w.sta_rssi} dBm`:'-';
  $('w_ap').innerHTML=w.ap_en?`<span class="dot ok"></span>${w.ap_ssid} · ${w.ap_ip} · ${w.ap_clients} kl.${w.ap_temp?' (failsafe)':''}`:'väljas';
  const cls=m.connected?'ok':(m.state=='SIM_ERROR'?'bad':(m.state=='DISABLED'?'':'warn'));
  $('m_state').innerHTML=`<span class="dot ${cls}"></span>${m.state=='DISABLED'?'väljas':m.state}`;
  $('m_op').innerHTML=m.operator=='-'?'-':`${m.operator}${m.tech!='-'?' ('+m.tech+')':''} · ${m.csq>=99?'-':bars(cq(m.csq))+m.rssi+' dBm'}`;
  $('m_ip').textContent=m.ip;$('m_err').textContent=m.error||'-';
  $('lteIp').innerHTML=m.connected?`📡 Mobiilivõrgu IP: <b>${m.ip}</b> (operaatorilt saadud)`:(m.state=='DISABLED'?'Mobiilivõrgu IP: – (LTE väljas)':`Mobiilivõrgu IP: – (IP-d pole veel saadud, olek ${m.state})`);
  if(s.rtsp_auth!==undefined&&s.rtsp_auth!==rtspAuth){rtspAuth=s.rtsp_auth;showRtsp()}
  $('c_res').textContent=c.sensor+' · '+c.res;$('c_fps').textContent=c.fps.toFixed(1)+' fps · '+c.frame_kb.toFixed(1)+' kB';
  $('c_af').textContent=c.af;$('c_rot').textContent=c.rotate+'°'+(c.rot_ms?` (${c.rot_ms} ms/kaader)`:'');
  $('c_cli').textContent=s.rtsp_clients+' / '+s.http_streams;
  $('s_up').textContent=dur(s.uptime);$('s_ram').textContent=kb(s.heap_free)+' / '+kb(s.heap_total);
  $('s_ps').textContent=kb(s.psram_free)+' / '+kb(s.psram_total);$('s_t').textContent=s.temp.toFixed(1)+' °C';$('s_fw').textContent=s.fw;
 }catch(e){if(e!==0)$('hdr').innerHTML='<span class="dot bad"></span>Seade ei vasta'}
 setTimeout(poll,3000)}
loadCfg();poll();otaPoll();
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
//  Sisselogimine
// -----------------------------------------------------------------------------
static const char LOGIN_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="et"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SimCam – sisselogimine</title><link rel="stylesheet" href="/style.css">
<style>
body{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.box{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:22px;width:100%;max-width:340px;display:grid;gap:12px}
h1{margin:0;font-size:20px}.err{color:var(--bad);font-size:14px;min-height:1em}
</style></head><body>
<form class="box" method="post" action="/login">
 <h1>SimCam</h1>
 <input type="password" name="pass" placeholder="Parool" autofocus autocomplete="current-password" required>
 <span class="err" id="err"></span>
 <button class="pri" type="submit">Logi sisse</button>
</form>
<script>if(location.search.indexOf('e=1')>=0)document.getElementById('err').textContent='Vale parool';</script>
</body></html>)HTML";
