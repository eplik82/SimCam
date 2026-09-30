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
input[type=text],input[type=password]{font:inherit;padding:9px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);width:100%}
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
@media (max-width:420px){.bar .lbl{display:none}.bar button{font-size:20px;padding:10px 12px}}
</style></head><body>
<div class="stage">
  <img id="view" alt="">
  <span class="msg" id="msg">Ühendan…</span>
  <a class="gear" href="/settings" title="Seaded">⚙</a>
</div>
<div class="bar">
  <button id="bPlay" title="Peata / jätka vaade">⏸<span class="lbl">Peata</span></button>
  <button id="bRotL" title="Pööra vastupäeva">↺<span class="lbl">90°</span></button>
  <button id="bRotR" title="Pööra päripäeva">↻<span class="lbl">90°</span></button>
  <button id="bFocus" title="Autofookus">◎<span class="lbl">Fookus</span></button>
  <button id="bSnap" title="Hetktõmmis">📷<span class="lbl">Hetktõmmis</span></button>
</div>
<div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);let playing=false,rot=0;
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),2200)}
function setPlay(p){playing=p;const v=$('view');
 if(p){v.src='/stream?'+Date.now();$('bPlay').innerHTML='⏸<span class="lbl">Peata</span>';$('bPlay').classList.remove('on')}
 else{v.removeAttribute('src');$('msg').textContent='Vaade peatatud';$('bPlay').innerHTML='▶<span class="lbl">Jätka</span>';$('bPlay').classList.add('on')}}
$('bPlay').onclick=()=>setPlay(!playing);
$('view').onload=()=>{$('msg').textContent=''};
$('view').onerror=()=>{if(playing){$('msg').textContent='Voog katkes – ühendan uuesti…';setTimeout(()=>playing&&setPlay(true),3000)}};
async function api(u,o){const r=await fetch(u,o);if(r.status==401){location.href='/login';throw 0}return r}
async function rotate(d){const n=(rot+d+360)%360;
 try{const r=await api('/api/cam?var=rotate&val='+n);if(r.ok){rot=n;toast('Pööre '+n+'°');if(playing)setPlay(true)}else toast('Pööramine ebaõnnestus')}catch(e){}}
$('bRotL').onclick=()=>rotate(-90);$('bRotR').onclick=()=>rotate(90);
$('bFocus').onclick=async()=>{const b=$('bFocus');b.disabled=true;
 try{const r=await api('/api/focus',{method:'POST'});toast(r.ok?'Autofookus käivitatud':'Autofookus pole saadaval')}catch(e){}
 setTimeout(()=>b.disabled=false,1500)};
$('bSnap').onclick=()=>{const a=document.createElement('a');const t=new Date().toISOString().slice(0,19).replace(/[:T]/g,'-');
 a.href='/capture?'+Date.now();a.download='simcam-'+t+'.jpg';document.body.appendChild(a);a.click();a.remove();toast('Hetktõmmis salvestatud')};
api('/api/cam').then(r=>r.json()).then(c=>{rot=c.rotate||0}).catch(()=>{});
setPlay(true);
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
//  Seaded ja olek
// -----------------------------------------------------------------------------
static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="et"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SimCam – seaded</title><link rel="stylesheet" href="/style.css">
<style>
header{display:flex;align-items:center;gap:12px;padding:12px 16px;border-bottom:1px solid var(--line);background:var(--card);position:sticky;top:0;z-index:2}
header h1{font-size:18px;margin:0;flex:1}
.dot{width:10px;height:10px;border-radius:50%;background:var(--mut);display:inline-block;margin-right:6px}
.dot.ok{background:var(--ok)}.dot.warn{background:var(--warn)}.dot.bad{background:var(--bad)}
main{max-width:1100px;margin:0 auto;padding:16px;display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));align-items:start}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 10px}
.row{display:flex;justify-content:space-between;gap:8px;padding:5px 0;border-bottom:1px dashed var(--line)}
.row:last-child{border:0}.row span:first-child{color:var(--mut)}
.row span:last-child{text-align:right;font-variant-numeric:tabular-nums;word-break:break-all}
.bars{display:inline-flex;gap:2px;align-items:flex-end;height:14px;margin-right:6px;vertical-align:-2px}
.bars i{width:4px;background:var(--line);border-radius:1px}.bars i.on{background:var(--ok)}
code{background:var(--bg);padding:2px 6px;border-radius:6px;font-size:13px}
form{display:grid;gap:8px}
label{display:flex;flex-direction:column;gap:3px;font-size:13px;color:var(--mut)}
label.chk{flex-direction:row;align-items:center;gap:8px;color:var(--fg);font-size:15px}
fieldset{border:1px solid var(--line);border-radius:8px;padding:8px 10px;display:grid;gap:8px;margin:0}
legend{font-size:12px;color:var(--mut);padding:0 4px}
.note{font-size:12px;color:var(--warn)}
.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
</style></head><body>
<header><a class="btn" href="/">← Vaade</a><h1>Seaded</h1><span id="hdr"><span class="dot"></span>…</span></header>
<main>
 <section class="card"><h2>Kaamera</h2>
  <div class="row"><span>RTSP</span><span><code id="rtsp">-</code></span></div>
  <div class="row"><span>Sensor / resolutsioon</span><span id="c_res">-</span></div>
  <div class="row"><span>FPS / kaadri suurus</span><span id="c_fps">-</span></div>
  <div class="row"><span>Autofookus</span><span id="c_af">-</span></div>
  <div class="row"><span>Pööre</span><span id="c_rot">-</span></div>
  <div class="row"><span>Vaatajad (RTSP / veeb)</span><span id="c_cli">-</span></div>
 </section>
 <section class="card"><h2>WiFi</h2>
  <div class="row"><span>Klient</span><span id="w_sta">-</span></div>
  <div class="row"><span>Klient IP / levi</span><span id="w_ip">-</span></div>
  <div class="row"><span>Hotspot</span><span id="w_ap">-</span></div>
  <div class="row"><span>Hotspot IP / kliente</span><span id="w_apip">-</span></div>
 </section>
 <section class="card"><h2>Mobiilivõrk</h2>
  <div class="row"><span>Olek</span><span id="m_state">-</span></div>
  <div class="row"><span>Operaator</span><span id="m_op">-</span></div>
  <div class="row"><span>Levi (CSQ / RSSI)</span><span id="m_rssi">-</span></div>
  <div class="row"><span>LTE RSRP / RSRQ</span><span id="m_rsrp">-</span></div>
  <div class="row"><span>IP aadress</span><span id="m_ip">-</span></div>
  <div class="row"><span>Viimane viga</span><span id="m_err">-</span></div>
 </section>
 <section class="card"><h2>Süsteem</h2>
  <div class="row"><span>Uptime</span><span id="s_up">-</span></div>
  <div class="row"><span>RAM (vaba / kokku)</span><span id="s_ram">-</span></div>
  <div class="row"><span>PSRAM (vaba / kokku)</span><span id="s_ps">-</span></div>
  <div class="row"><span>Kiibi temperatuur</span><span id="s_t">-</span></div>
  <div class="row"><span>Püsivara</span><span id="s_fw">-</span></div>
  <div class="btns"><button class="dng" id="bReboot">Taaskäivita</button><a class="btn" href="/logout">Logi välja</a></div>
 </section>
 <section class="card"><h2>WiFi ja LTE seaded</h2>
  <form id="cfg" autocomplete="off">
   <fieldset><legend>WiFi klient (ruuter)</legend>
    <label class="chk"><input type="checkbox" id="f_sta_en"> Ühendu WiFi võrku</label>
    <label>Võrgu nimi (SSID)<input type="text" id="f_sta_ssid" list="ssids" maxlength="32"></label>
    <datalist id="ssids"></datalist>
    <button type="button" id="bScan">Otsi võrke</button>
    <label>Parool <input type="password" id="f_sta_pass" maxlength="64" placeholder="(muutmata)"></label>
   </fieldset>
   <fieldset><legend>Hotspot (telefoniga otse)</legend>
    <label class="chk"><input type="checkbox" id="f_ap_en"> Hotspot sees</label>
    <label>Hotspoti nimi <input type="text" id="f_ap_ssid" maxlength="32"></label>
    <label>Hotspoti parool (min 8) <input type="password" id="f_ap_pass" maxlength="64" placeholder="(muutmata)"></label>
   </fieldset>
   <fieldset><legend>Mobiilivõrk</legend>
    <label class="chk"><input type="checkbox" id="f_lte_en"> LTE modem sees</label>
    <label>APN <input type="text" id="f_apn" maxlength="63" placeholder="nt operaatori staatilise IP APN"></label>
    <label>SIM PIN <input type="password" id="f_pin" maxlength="8" inputmode="numeric" autocomplete="off" placeholder="(muutmata)"></label>
    <label class="chk"><input type="checkbox" id="f_pin_clear"> SIM-il pole PIN-i (kustuta salvestatud PIN)</label>
    <label>Oodatav staatiline IP (valikuline) <input type="text" id="f_exp_ip" maxlength="15" placeholder="nt 213.x.x.x"></label>
    <span class="note">Ainult UART-modemiga (SIM7600 T-PCIe). MikroTik R11e-LTE puhul hoia väljas! Muudatused rakenduvad pärast taaskäivitust.</span>
   </fieldset>
   <button class="pri" type="submit">Salvesta</button>
  </form>
 </section>
 <section class="card"><h2>Püsivara</h2>
  <div class="row"><span>Praegune versioon</span><span id="o_cur">-</span></div>
  <div class="row"><span>Viimane GitHubis</span><span id="o_lat">-</span></div>
  <div class="row"><span>Olek</span><span id="o_st">-</span></div>
  <div id="o_barw" style="display:none;height:8px;background:var(--line);border-radius:4px;overflow:hidden;margin:8px 0"><div id="o_bar" style="height:100%;width:0;background:var(--acc)"></div></div>
  <div class="btns"><button id="bOtaChk">Kontrolli uuendusi</button><button class="pri" id="bOtaUpd" disabled>Uuenda</button></div>
 </section>
 <section class="card"><h2>Parool</h2>
  <div class="note" id="defpass" style="display:none;margin-bottom:8px">⚠ Kasutusel on vaikeparool – muuda see!</div>
  <form id="pw" autocomplete="off">
   <label>Uus parool (min 4 märki)<input type="password" id="p1" maxlength="64" autocomplete="new-password"></label>
   <label>Korda uut parooli<input type="password" id="p2" maxlength="64" autocomplete="new-password"></label>
   <span style="font-size:12px;color:var(--mut)">Sama parool kehtib RTSP-le: <code>rtsp://admin:parool@…/live</code></span>
   <button class="pri" type="submit">Muuda parool</button>
  </form>
 </section>
</main>
<div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),2500)}
async function api(u,o){const r=await fetch(u,o);if(r.status==401){location.href='/login';throw 0}return r}
function dur(s){s=Math.floor(s);const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
 return (d?d+'p ':'')+String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(s%60).padStart(2,'0')}
function kb(b){return b>1048576?(b/1048576).toFixed(2)+' MB':(b/1024).toFixed(0)+' kB'}
function bars(n){let h='<span class="bars">';for(let i=0;i<4;i++)h+=`<i class="${i<n?'on':''}" style="height:${4+i*3}px"></i>`;return h+'</span>'}
async function loadCfg(){try{const c=await (await api('/api/config',{cache:'no-store'})).json();
 $('f_sta_en').checked=c.sta_en;$('f_sta_ssid').value=c.sta_ssid;$('f_ap_en').checked=c.ap_en;
 $('f_ap_ssid').value=c.ap_ssid;$('f_lte_en').checked=c.lte_en;
 $('f_sta_pass').placeholder=c.sta_has_pass?'(muutmata)':'(avatud võrk)'}catch(e){}}
$('bScan').onclick=async()=>{const b=$('bScan');b.disabled=true;b.textContent='Otsin…';
 try{const l=await (await api('/api/scan',{cache:'no-store'})).json();l.sort((a,b)=>b.rssi-a.rssi);
  $('ssids').innerHTML=l.map(n=>`<option value="${n.ssid.replace(/"/g,'&quot;')}">${n.rssi} dBm${n.enc?' 🔒':''}</option>`).join('');
  toast(l.length+' võrku leitud');$('f_sta_ssid').focus()}catch(e){toast('Otsing ebaõnnestus')}
 b.disabled=false;b.textContent='Otsi võrke'};
$('cfg').onsubmit=async e=>{e.preventDefault();const p=new URLSearchParams();
 p.set('sta_en',$('f_sta_en').checked?'1':'0');p.set('sta_ssid',$('f_sta_ssid').value);p.set('sta_pass',$('f_sta_pass').value);
 p.set('ap_en',$('f_ap_en').checked?'1':'0');p.set('ap_ssid',$('f_ap_ssid').value);p.set('ap_pass',$('f_ap_pass').value);
 p.set('lte_en',$('f_lte_en').checked?'1':'0');
 if($('f_ap_pass').value&&$('f_ap_pass').value.length<8){toast('Hotspoti parool peab olema vähemalt 8 märki');return}
 try{const d=await (await api('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})).json();
  if(!d.ok){toast(d.error||'Viga');return}
  $('f_sta_pass').value='';$('f_ap_pass').value='';$('f_pin').value='';$('f_pin_clear').checked=false;loadCfg();
  toast(d.reboot?'Salvestatud – LTE muutus rakendub pärast taaskäivitust':'Salvestatud – WiFi taaskäivitub')}catch(e){toast('Salvestatud – WiFi taaskäivitub')}};
$('pw').onsubmit=async e=>{e.preventDefault();const a=$('p1').value,b=$('p2').value;
 if(a.length<4){toast('Parool peab olema vähemalt 4 märki');return}if(a!==b){toast('Paroolid ei kattu');return}
 try{const r=await api('/api/password',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({pass:a})});
  toast(r.ok?'Parool muudetud':'Parooli muutmine ebaõnnestus');if(r.ok){$('p1').value='';$('p2').value=''}}catch(e){}};
const OST={idle:'-',checking:'Kontrollin…',uptodate:'Ajakohane ✓',available:'Uuendus saadaval!',updating:'Uuendan…',done:'Paigaldatud – taaskäivitub',error:'Viga'};
async function otaPoll(){try{const o=await (await api('/api/ota',{cache:'no-store'})).json();
 $('o_cur').textContent=o.current;$('o_lat').textContent=o.latest?o.latest+(o.name&&o.name!=('v'+o.latest)?' – '+o.name:''):'-';
 let st=OST[o.state]||o.state;if(o.state=='error')st+=': '+o.error;if(o.state=='updating')st+=' '+o.progress+'%';
 if(o.checked_ago&&o.state!='updating')st+=` (kontrollitud ${Math.round(o.checked_ago/60)} min tagasi)`;
 $('o_st').textContent=st;$('bOtaUpd').disabled=o.state!='available';$('bOtaChk').disabled=o.state=='checking'||o.state=='updating';
 $('o_barw').style.display=(o.state=='updating'||o.state=='done')?'block':'none';$('o_bar').style.width=o.progress+'%';
 setTimeout(otaPoll,(o.state=='updating'||o.state=='checking')?1000:5000)}catch(e){setTimeout(otaPoll,5000)}}
$('bOtaChk').onclick=async()=>{try{await api('/api/ota/check',{method:'POST'});$('o_st').textContent='Kontrollin…'}catch(e){}};
$('bOtaUpd').onclick=async()=>{if(!confirm('Paigaldada uus püsivara? Seade taaskäivitub. Kui uus versioon ei tööta, taastatakse eelmine automaatselt.'))return;
 try{const r=await api('/api/ota/update',{method:'POST'});toast(r.ok?'Uuendus alustatud':'Uuendust pole saadaval')}catch(e){}};
$('bReboot').onclick=async()=>{if(!confirm('Kas taaskäivitada seade?'))return;try{await api('/api/reboot',{method:'POST'})}catch(e){}toast('Taaskäivitan…')};
async function poll(){
 try{const d=await (await api('/api/status',{cache:'no-store'})).json();const m=d.lte,c=d.cam,s=d.sys,w=d.wifi;
  const on=[];if(w.sta_ok)on.push('WiFi');if(w.ap_clients)on.push('Hotspot');if(m.connected)on.push('LTE');
  $('hdr').innerHTML=`<span class="dot ${on.length?'ok':'warn'}"></span>${on.length?on.join(' + '):'Võrguühendus puudub'}`;
  const wq=q=>q>=-55?4:q>=-65?3:q>=-75?2:q>=-85?1:0,cq=q=>q>=99?0:q>=20?4:q>=15?3:q>=10?2:q>=2?1:0;
  $('w_sta').innerHTML=w.sta_en?`<span class="dot ${w.sta_ok?'ok':'warn'}"></span>${w.sta_ssid}${w.sta_ok?'':' (ühendun…)'}`:'väljas';
  $('w_ip').innerHTML=w.sta_ok?`${w.sta_ip} / ${bars(wq(w.sta_rssi))}${w.sta_rssi} dBm`:'-';
  $('w_ap').innerHTML=w.ap_en?`<span class="dot ok"></span>${w.ap_ssid}`:'väljas';
  $('w_apip').textContent=w.ap_en?`${w.ap_ip} / ${w.ap_clients}`:'-';
  const cls=m.connected?'ok':(m.state=='SIM_ERROR'?'bad':(m.state=='DISABLED'?'':'warn'));
  $('m_state').innerHTML=`<span class="dot ${cls}"></span>${m.state=='DISABLED'?'välja lülitatud':m.state}`;
  $('m_op').textContent=m.operator+(m.tech!='-'?' ('+m.tech+')':'');
  $('m_rssi').innerHTML=m.csq>=99?'-':bars(cq(m.csq))+m.csq+' / '+m.rssi+' dBm';
  $('m_rsrp').textContent=m.rsrp?m.rsrp+' dBm / '+m.rsrq+' dB':'-';
  $('m_ip').textContent=m.ip;$('m_err').textContent=m.error||'-';
  $('rtsp').textContent=`rtsp://admin:***@${location.hostname}:554/live`;
  $('c_res').textContent=c.sensor+' / '+c.res;$('c_fps').textContent=c.fps.toFixed(1)+' / '+c.frame_kb.toFixed(1)+' kB';
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
