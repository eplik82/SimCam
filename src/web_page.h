// =============================================================================
//  Veebiliidese lehed (HTML + CSS + JS) – talletatud flash-mälus (PROGMEM)
//   INDEX_HTML    – vaade: ainult kaamerapilt + nupud
//   SETTINGS_HTML – olek, WiFi/LTE seaded, parool, taaskäivitus
//   LOG_HTML      – seadme logi (jooksev + eelmine käivitus)
//   AUDIO_JS      – brauseri helimängija (/audio.js)
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
<title>{{NAME}}</title><link rel="stylesheet" href="/style.css">
<style>
html,body{height:100%;background:#000}
body{display:flex;flex-direction:column;color:#fff}
.stage{flex:1;min-height:0;display:flex;align-items:center;justify-content:center;position:relative;overflow:hidden}
.stage img{max-width:100%;max-height:100%;object-fit:contain;display:block}
.msg{position:absolute;color:#9aa;font-size:15px;text-align:center;padding:16px}
.cname{position:absolute;top:10px;left:10px;max-width:calc(100% - 80px);background:rgba(0,0,0,.45);border:1px solid rgba(255,255,255,.2);color:#fff;border-radius:10px;padding:6px 12px;font-size:16px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;z-index:2}
.gear{position:absolute;top:10px;right:10px;background:rgba(0,0,0,.45);border:1px solid rgba(255,255,255,.2);color:#fff;border-radius:50%;width:42px;height:42px;display:flex;align-items:center;justify-content:center;font-size:20px;text-decoration:none}
.bar{display:flex;justify-content:center;gap:8px;flex-wrap:wrap;padding:10px 10px calc(10px + env(safe-area-inset-bottom));background:#0d0f14;border-top:1px solid #222}
.bar button{background:#1b2029;border-color:#2a303b;color:#e8ebf1;min-width:52px;justify-content:center;padding:10px 14px;font-size:15px}
.bar button.on{background:var(--acc);border-color:var(--acc);color:#fff}
.bar .lbl{font-size:13px}
.info{display:flex;justify-content:center;align-items:center;gap:10px;flex-wrap:wrap;padding:6px 10px;background:#0d0f14;color:#aab2c0;font-size:13px;font-variant-numeric:tabular-nums;border-top:1px solid #222;min-height:30px}
.info b{color:#e8ebf1;font-weight:600}
.info .warn{color:#f0b429}
.info .low{color:#ff6b6b;font-weight:600}
.focus{display:none;align-items:center;gap:10px;padding:6px 14px;background:#0d0f14;color:#aab2c0;font-size:13px;border-top:1px solid #222}
.focus.show{display:flex}
.focus input{flex:1;min-width:0;accent-color:var(--acc);height:28px}
.focus b{color:#e8ebf1;font-variant-numeric:tabular-nums;min-width:34px;text-align:right}
.mbar{display:inline-block;width:46px;height:7px;background:#2a303b;border-radius:4px;overflow:hidden;vertical-align:1px;margin-left:4px}
.mbar>i{display:block;height:100%;background:#2fbf71;transition:width .3s}
.rot{display:inline-flex;border:1px solid #2a303b;border-radius:10px;overflow:hidden}
.bar .rot button{border:0;border-radius:0;min-width:48px;padding:10px 10px;border-right:1px solid #2a303b}
.bar .rot button:last-child{border-right:0}
@media (max-width:420px){.bar .lbl{display:none}.bar button{font-size:18px;padding:10px 11px}.bar .rot button{font-size:14px;min-width:42px}}
/* Telefon horisontaalis: pilt üle kogu ekraani, info ja nupud pildi peal */
@media (orientation:landscape) and (max-height:600px){
 .stage{position:fixed;inset:0}
 .stage img{width:100%;height:100%}
 .cname{position:fixed;top:calc(8px + env(safe-area-inset-top));left:calc(8px + env(safe-area-inset-left));font-size:14px;padding:4px 10px}
 .info{position:fixed;top:calc(44px + env(safe-area-inset-top));left:calc(8px + env(safe-area-inset-left));z-index:2;background:rgba(0,0,0,.5);border:0;border-radius:8px;min-height:0;padding:4px 10px;font-size:12px}
 .bar{position:fixed;left:0;right:0;bottom:0;z-index:2;background:linear-gradient(transparent,rgba(0,0,0,.65));border:0;padding:16px calc(10px + env(safe-area-inset-right)) calc(8px + env(safe-area-inset-bottom)) calc(10px + env(safe-area-inset-left))}
 .bar button,.bar .rot{background:rgba(27,32,41,.72);border-color:rgba(255,255,255,.15)}
 .bar .rot button{background:transparent}
 .bar button.on,.bar .rot button.on{background:var(--acc);border-color:var(--acc)}
 .bar .lbl{display:none}
 .gear{top:calc(10px + env(safe-area-inset-top));right:calc(10px + env(safe-area-inset-right));z-index:3}
 .toast{bottom:76px}
 .focus{position:fixed;left:calc(12px + env(safe-area-inset-left));right:calc(12px + env(safe-area-inset-right));bottom:64px;z-index:2;background:rgba(0,0,0,.5);border:0;border-radius:10px;max-width:520px;margin:0 auto}
}
</style></head><body>
<div class="stage">
  <img id="view" alt="">
  <span class="msg" id="msg">Ühendan…</span>
  <span class="cname">{{NAME}}</span>
  <a class="gear" href="/settings" title="Seaded">⚙</a>
</div>
<div class="info" id="info"><span id="iBat" style="display:none"></span><span id="iMic" style="display:none"></span><span id="iStat">Ühendan…</span></div>
<div class="focus" id="focusRow"><span title="Kaugele">🏔</span><input type="range" id="fpos" min="0" max="1023" step="1" aria-label="Fookus (läätse asend)"><span title="Lähedale">🌼</span><b id="fposT">-</b></div>
<div class="bar">
  <button id="bPlay" title="Peata / jätka vaade">⏸<span class="lbl">Peata</span></button>
  <span class="rot" title="Pildi pööre"><button data-r="0">0°</button><button data-r="180">180°</button></span>
  <button id="bFocus" title="Autofookus">◎<span class="lbl">Fookus</span></button>
  <button id="bSnap" title="Hetktõmmis">📷<span class="lbl">Hetktõmmis</span></button>
  <button id="bAudio" title="Heli sisse / välja" style="display:none">🔈<span class="lbl">Heli</span></button>
</div>
<div class="toast" id="toast"></div>
<script src="/audio.js"></script>
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
function showRot(){document.querySelectorAll('.rot button').forEach(b=>b.classList.toggle('on',+b.dataset.r===rot))}
async function rotate(n){if(n===rot)return;
 try{const r=await api('/api/cam?var=rotate&val='+n);if(r.ok){rot=n;showRot();toast('Pööre '+n+'°');if(playing)setPlay(true)}else toast('Pööramine ebaõnnestus')}catch(e){}}
document.querySelectorAll('.rot button').forEach(b=>b.onclick=()=>rotate(+b.dataset.r));
async function stats(){
 if(!playing){$('iStat').textContent='Vaade peatatud'}
 else try{const d=await (await api('/api/view?id='+vid,{cache:'no-store'})).json();
  if(d.rotate!==rot){rot=d.rotate;showRot()}
  const b=d.bat,ib=$('iBat');ib.style.display=b&&b.enabled&&b.state!='ABSENT'?'':'none';
  const mc=d.mic,im=$('iMic');$('bAudio').style.display=mc&&mc.on?'':'none';im.style.display=mc&&mc.on&&SimAudio.active()?'':'none';
  if(mc&&mc.on){const lv=Math.max(0,Math.min(100,(mc.level+70)/70*100));im.innerHTML=`🎤<span class="mbar"><i style="width:${lv.toFixed(0)}%"></i></span>`}
  else if(SimAudio.active())SimAudio.stop();
  if(b&&b.enabled){ib.className=b.state=='LOW'?'low':'';ib.title='Aku '+b.v.toFixed(2)+' V';
   ib.textContent=(b.state=='CHARGING'?'⚡':b.state=='LOW'?'🪫':'🔋')+' '+b.pct+' %'}
  if(d.fps<0)$('iStat').textContent='Ühendan…';
  else $('iStat').innerHTML=`<b>${d.fps.toFixed(1)} fps</b> · <b>${d.kBps.toFixed(0)} kB/s</b> (${(d.kBps*8/1024).toFixed(2)} Mbit/s) · kaader ${d.frame_kb.toFixed(1)} kB`;
 }catch(e){}
 setTimeout(stats,2000)}
$('bFocus').onclick=async()=>{const b=$('bFocus');b.disabled=true;
 try{const r=await api('/api/focus',{method:'POST'});toast(r.ok?'Autofookus käivitatud':'Autofookus pole saadaval')}catch(e){}
 setTimeout(()=>b.disabled=false,1500)};
$('bSnap').onclick=()=>{const a=document.createElement('a');const t=new Date().toISOString().slice(0,19).replace(/[:T]/g,'-');
 a.href='/capture?'+Date.now();a.download='simcam-'+t+'.jpg';document.body.appendChild(a);a.click();a.remove();toast('Hetktõmmis salvestatud')};
SimAudio.onstate=on=>{const b=$('bAudio');b.classList.toggle('on',on);b.firstChild.textContent=on?'🔊':'🔈'};
SimAudio.onerror=m=>toast('Heli: '+m);
$('bAudio').onclick=()=>SimAudio.active()?SimAudio.stop():SimAudio.start();
// Käsitsi fookus: liugur nähtav ainult siis, kui seadetes on fookuse režiim "Käsitsi"
let fDrag=false,fT=0;
async function camInfo(){try{const c=await (await api('/api/cam',{cache:'no-store'})).json();
 rot=c.rotate||0;showRot();
 const man=c.af&&c.af_mode==2;$('focusRow').classList.toggle('show',man);
 if(man&&!fDrag){$('fpos').value=c.af_pos;$('fposT').textContent=c.af_pos}}catch(e){}}
$('fpos').oninput=e=>{fDrag=true;$('fposT').textContent=e.target.value;const n=Date.now();
 if(n-fT>150){fT=n;api('/api/cam?var=af_pos_live&val='+e.target.value).catch(()=>{})}};
$('fpos').onchange=async e=>{try{await api('/api/cam?var=af_pos&val='+e.target.value)}catch(x){}fDrag=false};
camInfo();setInterval(camInfo,10000);
// Horisontaalis täisekraan (brauser lubab seda ainult pärast puudutust → proovi ka esimesel puudutusel)
const land=matchMedia('(orientation:landscape) and (max-height:600px)'),de=document.documentElement;
function fsOn(){if(land.matches&&!document.fullscreenElement&&de.requestFullscreen)de.requestFullscreen({navigationUI:'hide'}).catch(()=>{})}
land.addEventListener('change',()=>{if(land.matches)fsOn();else if(document.fullscreenElement)document.exitFullscreen().catch(()=>{})});
document.addEventListener('click',fsOn);
setPlay(true);stats();
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
//  Seaded ja olek
// -----------------------------------------------------------------------------
static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="et"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{NAME}} – seaded</title><link rel="stylesheet" href="/style.css">
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
.nets{display:none;grid-template-columns:minmax(0,1fr);gap:4px;max-height:280px;overflow:auto;border:1px solid var(--line);border-radius:10px;padding:4px}
.nets.show{display:grid}
.nets button{box-sizing:border-box;width:100%;min-width:0;justify-content:space-between;gap:10px;border:0;border-radius:8px;padding:11px 10px;text-align:left}
.nets button:hover,.nets button:active{background:var(--bg)}
.nets button>span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;min-width:0}
.nets small{color:var(--mut);flex:none;white-space:nowrap}
.bat{display:flex;align-items:center;gap:14px;margin-bottom:6px}
.bat .pct{font-size:30px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1}
.bat .st{font-size:14px;color:var(--mut)}
.meter{height:10px;background:var(--line);border-radius:5px;overflow:hidden;margin:2px 0 8px}
.meter>div{height:100%;width:0;background:var(--ok);border-radius:5px;transition:width .4s}
.meter.low>div{background:var(--bad)}.meter.mid>div{background:var(--warn)}
.chart{position:relative;margin:8px 0 2px}
.chart svg{display:block;width:100%;height:150px;touch-action:none}
.chart .tip{position:absolute;top:0;pointer-events:none;background:var(--fg);color:var(--bg);font-size:12px;padding:3px 7px;border-radius:6px;white-space:nowrap;display:none;transform:translateX(-50%)}
.rng{display:flex;gap:4px;justify-content:flex-end}.rng button{padding:3px 9px;font-size:12px}
.rng button.on{background:var(--acc);border-color:var(--acc);color:#fff}
details summary{cursor:pointer;color:var(--mut);font-size:13px;margin-top:8px}
input[type=range]{width:100%;accent-color:var(--acc)}
select.sel{width:100%;min-width:0;max-width:100%;font:inherit;font-size:15px;padding:8px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg)}
.lvl{height:8px;background:var(--line);border-radius:4px;overflow:hidden}.lvl>div{height:100%;width:0;background:var(--ok);transition:width .2s}
.lvl.hot>div{background:var(--bad)}
details form{margin-top:8px}
.grid{grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}
.grp>details>summary{list-style:none;cursor:pointer;margin:0;color:var(--fg);font-size:inherit}
.grp>details>summary::-webkit-details-marker{display:none}
.grp>details>summary h2{margin:0}
.grp>details>summary h2::after{content:"▾";margin-left:auto;color:var(--mut);font-size:13px;transition:transform .2s}
.grp>details:not([open])>summary h2::after{transform:rotate(-90deg)}
.grp>details[open]>summary{margin-bottom:10px}
.two{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
.tabs{display:flex;gap:4px;background:var(--bg);border-radius:10px;padding:3px;margin-bottom:10px}
.tabs button{flex:1;justify-content:center;border:0;background:transparent;padding:7px 8px;font-size:14px}
.tabs button.on{background:var(--card);box-shadow:0 1px 3px rgba(0,0,0,.15);font-weight:600}
.tab{display:grid;gap:10px}
.tab[hidden],details[hidden]{display:none}
small.mut{color:var(--mut);font-size:12px}
details summary{margin-top:10px}
</style></head><body>
<header><a class="btn" href="/">← Vaade</a><h1>{{NAME}} – seaded</h1><span id="hdr"><span class="dot"></span>…</span></header>
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
 <section class="card" id="batCard"><h2>🔋 Aku</h2>
  <div id="batOn">
   <div class="bat"><span class="pct" id="b_pct">–</span><span class="st" id="b_st">Mõõdan…</span></div>
   <div class="meter" id="b_meter"><div id="b_bar"></div></div>
   <div class="row"><span>Pinge</span><span id="b_v">-</span></div>
   <div class="row"><span>Pinge muutus (10 min)</span><span id="b_sl">-</span></div>
   <div class="row"><span>Hinnanguline tööaeg</span><span id="b_left">-</span></div>
   <div class="row"><span>USB arvutiga</span><span id="b_usb">-</span></div>
   <details id="b_graph"><summary>Pinge graafik</summary>
    <div class="chart" id="b_chart"><svg id="b_svg" role="img" aria-label="Aku pinge ajalugu"></svg><span class="tip" id="b_tip"></span></div>
    <div class="rng" id="b_rng"><button data-h="1">1 h</button><button data-h="6">6 h</button><button data-h="24" class="on">24 h</button></div>
   </details>
  </div>
  <div class="note" id="batOff" style="display:none">Aku jälgimine on välja lülitatud.</div>
  <details><summary>Aku seaded ja kalibreerimine</summary>
   <form id="fBat">
    <label class="chk"><input type="checkbox" id="f_bat_en"> Aku on ühendatud</label>
    <label>Multimeetriga mõõdetud aku pinge (V)<span class="inl"><input type="text" inputmode="decimal" id="f_bat_v" placeholder="nt 3,95"><button type="submit">Kalibreeri</button></span></label>
    <div class="note">Kalibreerimistegur: <span id="b_cal">-</span> · <a href="#" id="bCalReset">lähtesta</a><br>
    Plaadil (TP4056 laadija, pingejagur GPIO3-l) pole voolu mõõtmist ega laadija oleku viiku – olek ja tööaeg arvutatakse pinge muutumise järgi. Laadimise ajal on pinge ja % tegelikust kõrgemad.</div>
   </form>
  </details>
 </section>
 <section class="card"><h2>⚙ Süsteem</h2>
  <div class="row"><span>Tööaeg</span><span id="s_up">-</span></div>
  <div class="row"><span>RAM vaba</span><span id="s_ram">-</span></div>
  <div class="row"><span>PSRAM vaba</span><span id="s_ps">-</span></div>
  <div class="row"><span>Temperatuur</span><span id="s_t">-</span></div>
  <div class="row"><span>Püsivara</span><span id="s_fw">-</span></div>
  <div class="row"><span>Viimane käivitus</span><span id="s_rst">-</span></div>
  <div class="row"><span>Logi</span><span id="s_log">-</span></div>
  <div class="btns" style="margin-top:10px"><a class="btn" href="/log">📄 Logi</a><button class="dng" id="bReboot">Taaskäivita</button><a class="btn" href="/logout">Logi välja</a></div>
 </section>
</div>

<h3>Seaded</h3>
<div class="cols">
 <section class="card grp" data-g="cam"><details open><summary><h2>📷 Kaamera</h2></summary>
  <form id="fCamAll" autocomplete="off">
   <div class="two">
    <label>Kaamera nimi<input type="text" id="f_cam_name" maxlength="40" placeholder="nt Garaaž, Suvila õu"></label>
    <label>Resolutsioon<select class="sel" id="f_fs"></select></label>
   </div>
   <div class="note">Nimi on pealehe ülaservas, vahelehel, sisselogimisel, logis ja RTSP voo nimena. Mobiilivõrgus (LTE) soovitame resolutsiooni SVGA 800×600 või väiksemat.</div>
   <div class="btns"><button class="pri" type="submit">Salvesta</button></div>
  </form>
  <hr>
  <label class="chk" style="margin-top:6px"><input type="checkbox" id="f_mirror"> Peegelda pilt (vasak ↔ parem) <small class="mut">– rakendub kohe</small></label>
  <details id="afBox" style="display:none"><summary>Fookus · <span id="af_st">-</span></summary>
   <label style="margin-top:8px">Režiim
    <select class="sel" id="f_af_mode"><option value="0">Ühekordne (soovitatav)</option><option value="1">Pidev</option><option value="2">Käsitsi</option></select></label>
   <div class="note">Ühekordne: teravustab käivitusel ja „Fookus" nupuga, siis lääts jääb paigale. Pidev: teravustab ise ümber (võib „hüpata"). Käsitsi: liugur ka pealehel.</div>
   <div id="afMan" style="display:none;margin-top:6px">
    <div class="row"><span>Läätse asend</span><span id="af_pos_t">-</span></div>
    <input type="range" id="f_af_pos" min="0" max="1023" step="1" aria-label="Läätse asend">
    <div class="note">← kaugele · lähedale →</div>
   </div>
   <div class="btns" style="margin-top:6px"><button type="button" id="bAfNow">◎ Fokusseeri</button></div>
  </details>
 </details></section>

 <section class="card grp" data-g="net"><details open><summary><h2>🌐 Võrk</h2></summary>
  <div class="tabs" role="tablist"><button type="button" class="on" data-t="tWifi">WiFi</button><button type="button" data-t="tAp">Hotspot</button><button type="button" data-t="tLte">LTE</button></div>
  <form id="fNet" autocomplete="off">
   <div class="tab" id="tWifi">
    <label class="chk"><input type="checkbox" id="f_sta_en"> Ühendu WiFi võrku</label>
    <label>Võrgu nimi (SSID)<span class="inl"><input type="text" id="f_sta_ssid" maxlength="32" autocapitalize="off" autocorrect="off" spellcheck="false"><button type="button" id="bScan">Otsi võrke</button></span></label>
    <div class="nets" id="ssids"></div>
    <label>Parool<input type="password" id="f_sta_pass" maxlength="64" placeholder="(muutmata)"></label>
   </div>
   <div class="tab" id="tAp" hidden>
    <label class="chk"><input type="checkbox" id="f_ap_en"> Hotspot sees</label>
    <div class="two"><label>Nimi<input type="text" id="f_ap_ssid" maxlength="32"></label>
     <label>Parool (min 8)<input type="password" id="f_ap_pass" maxlength="64" placeholder="(muutmata)"></label></div>
    <div class="note">Kui WiFi ja LTE on mõlemad väljas, jääb hotspot alati sisse. Kui võrguühendus kaob 2 minutiks, lülitub hotspot ajutiselt ise sisse.</div>
   </div>
   <div class="tab" id="tLte" hidden>
    <label class="chk"><input type="checkbox" id="f_lte_en"> LTE modem sees</label>
    <div class="two"><label>APN<input type="text" id="f_apn" maxlength="63" placeholder="operaatori APN"></label>
     <label>SIM PIN<input type="password" id="f_pin" maxlength="8" inputmode="numeric" placeholder="(muutmata)"></label></div>
    <label class="chk"><input type="checkbox" id="f_pin_clear"> SIM-il pole PIN-i</label>
    <div class="ipbox" id="lteIp">Mobiilivõrgu IP: –</div>
    <div class="note">Ainult UART-modemiga (LilyGO T-PCIe SIM7600). MikroTik R11e-LTE puhul hoia väljas! LTE muudatused rakenduvad pärast taaskäivitust.</div>
   </div>
   <div class="btns"><button class="pri" type="submit">Salvesta</button></div>
  </form>
  <details id="simBox" hidden><summary>Täpsemalt: muuda SIM-kaardi PIN-i</summary>
   <form id="fSim" autocomplete="off">
    <div class="note">Muudab PIN-koodi SIM-kaardil endal (modem peab olema võrku ühendatud). Uus PIN salvestatakse ka seadmesse.</div>
    <label>Praegune PIN<input type="password" id="s_old" maxlength="8" inputmode="numeric"></label>
    <div class="two"><label>Uus PIN (4–8 numbrit)<input type="password" id="s_new" maxlength="8" inputmode="numeric"></label>
     <label>Korda uut PIN-i<input type="password" id="s_new2" maxlength="8" inputmode="numeric"></label></div>
    <div class="warn">3 valet katset lukustab SIM-i (vaja PUK-koodi).</div>
    <div class="btns"><button type="submit">Muuda SIM PIN</button></div>
   </form>
  </details>
 </details></section>

 <section class="card grp" data-g="stream"><details open><summary><h2>📡 Voogedastus</h2></summary>
  <div class="url"><code id="rtsp">-</code><button type="button" id="bCopy" title="Kopeeri aadress">⧉</button></div>
  <div class="row" id="rtspCred"><span>Kasutaja / parool</span><span><b>admin</b> / veebiliidese parool</span></div>
  <div class="note">Salvestis/VMS (Synology Surveillance Station, Milestone, Blue Iris …): kaamera tüüp „RTSP" / „Generic", aadressiks ülal olev rida, kasutaja ja parool eraldi väljadele. Video: MJPEG, heli G.711. VLC: Meedia → Ava võrguvoog.</div>
  <form id="fStream" autocomplete="off">
   <label class="chk"><input type="checkbox" id="f_rtsp_auth"> RTSP nõuab parooli</label>
   <hr>
   <label class="chk"><input type="checkbox" id="f_push_en"> RTSP push serverisse <small class="mut">(nt MediaMTX)</small></label>
   <div id="pushBox">
    <div class="note">Seade saadab voo ise serverisse – töötab ka mobiilivõrgus, kus seadmele väljast ligi ei pääse. NB: push hoiab kaamera ja mikrofoni pidevalt töös.</div>
    <label>Serveri aadress<input type="text" id="f_push_url" maxlength="159" placeholder="rtsp://server.ee:8554/simcam" autocapitalize="off" spellcheck="false"></label>
    <div class="two"><label>Kasutaja<input type="text" id="f_push_user" maxlength="47" autocomplete="off" autocapitalize="off"></label>
     <label>Parool<input type="password" id="f_push_pass" maxlength="63" autocomplete="new-password"></label></div>
    <div class="row"><span>Olek</span><span id="push_st">-</span></div>
    <div class="row"><span>Voog</span><span id="push_rate">-</span></div>
    <div class="note" id="push_err"></div>
   </div>
   <div class="btns"><button class="pri" type="submit">Salvesta</button></div>
  </form>
 </details></section>

 <section class="card grp" data-g="mic"><details open><summary><h2>🎤 Heli</h2></summary>
  <form id="fMic">
   <label class="chk"><input type="checkbox" id="f_mic_en"> Mikrofon sees <small class="mut">– rakendub kohe</small></label>
   <div id="micOn">
    <div class="row"><span>Helitase</span><span id="m_lvl_t">-</span></div>
    <div class="lvl" id="m_lvl"><div id="m_lvl_b"></div></div>
    <div class="row" style="margin-top:6px"><span>Võimendus</span><span id="m_gain_t">24 dB</span></div>
    <input type="range" id="f_mic_gain" min="0" max="40" step="2" value="24" aria-label="Võimendus">
    <details><summary>Täpsemalt</summary>
     <label style="margin-top:8px">Helikvaliteet
      <select class="sel" id="f_mic_codec"><option value="0">G.711 8 kHz, 64 kbit/s (soovitatav)</option><option value="1">L16 16 kHz, 256 kbit/s (selgem)</option></select></label>
     <label class="chk" style="margin-top:8px"><input type="checkbox" id="f_rtsp_audio"> Heli RTSP voos</label>
     <div class="note">Kodeki muutus rakendub uutele RTSP ühendustele.</div>
    </details>
    <div class="btns" style="margin-top:8px"><button type="button" id="bListen">🔈 Kuula</button><button class="pri" type="submit">Salvesta</button></div>
   </div>
   <div class="note">Mikrofon on vaikimisi väljas ja töötab ainult pildi vaatamise või „Kuula" ajal. Arvesta teiste inimeste privaatsusega.</div>
  </form>
 </details></section>

 <section class="card grp" data-g="sys"><details open><summary><h2>🔒 Turvalisus ja püsivara</h2></summary>
  <div class="warn" id="defpass" style="display:none">⚠ Kasutusel on vaikeparool – muuda see!</div>
  <form id="fPw" autocomplete="off">
   <div class="two"><label>Uus parool (veeb + RTSP, min 4)<input type="password" id="p1" maxlength="64" autocomplete="new-password"></label>
    <label>Korda uut parooli<input type="password" id="p2" maxlength="64" autocomplete="new-password"></label></div>
   <div class="btns"><button type="submit">Muuda parool</button></div>
  </form>
  <hr>
  <div class="row"><span>Püsivara</span><span id="o_cur">-</span></div>
  <div class="row"><span>Viimane GitHubis</span><span id="o_lat">-</span></div>
  <div class="row"><span>Olek</span><span id="o_st">-</span></div>
  <div class="bar" id="o_barw" style="display:none;margin:8px 0"><div id="o_bar"></div></div>
  <label class="chk" style="margin-top:8px"><input type="checkbox" id="f_auto"> Uuenda automaatselt</label>
  <div class="note" id="o_autonote">Kontroll 1 min pärast käivitust ja iga 6 h järel; uus versioon paigaldatakse ise. Kui see ei käivitu, taastatakse eelmine ja seda versiooni enam ei proovita.</div>
  <div class="btns" style="margin-top:8px"><button id="bOtaChk">Kontrolli uuendusi</button><button class="pri" id="bOtaUpd" disabled>Uuenda</button></div>
 </details></section>
</div>
</main>
<div class="toast" id="toast"></div>
<script src="/audio.js"></script>
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
 if(document.activeElement!==$('f_cam_name'))$('f_cam_name').value=c.cam_name||'';
 $('f_push_en').checked=c.push_en;$('f_push_url').value=c.push_url||'';$('f_push_user').value=c.push_user||'';
 $('f_push_pass').value='';$('f_push_pass').placeholder=c.push_has_pass?'(muutmata)':'(puudub)';pushVis();
 $('f_sta_en').checked=c.sta_en;$('f_sta_ssid').value=c.sta_ssid;$('f_sta_pass').placeholder=c.sta_has_pass?'(muutmata)':'(avatud võrk)';
 $('f_ap_en').checked=c.ap_en;$('f_ap_ssid').value=c.ap_ssid;
 $('f_lte_en').checked=c.lte_en;$('f_apn').value=c.apn;
 $('f_pin').placeholder=c.has_pin?'(salvestatud)':'(puudub)';$('f_pin_clear').checked=false;
 $('f_rtsp_auth').checked=c.rtsp_auth;rtspAuth=c.rtsp_auth;showRtsp();
 curFs=String(c.framesize);
 if(c.framesizes){const sel=$('f_fs');sel.innerHTML=c.framesizes.map(f=>`<option value="${f.v}">${f.name} – ${f.w}×${f.h}${f.v==c.framesize?' (praegu)':''}</option>`).join('');sel.value=c.framesize}
 $('f_mic_en').checked=c.mic_en;$('f_mic_gain').value=c.mic_gain;$('m_gain_t').textContent=c.mic_gain+' dB';
 $('f_mic_codec').value=c.mic_codec;$('f_rtsp_audio').checked=c.rtsp_audio;$('micOn').style.display=c.mic_en?'':'none';
 $('defpass').style.display=c.default_pass?'block':'none'}catch(e){}}
function showRtsp(){$('rtsp').textContent=`rtsp://${location.hostname}:554/live`;$('rtspCred').style.display=rtspAuth?'':'none'}
$('bCopy').onclick=()=>{const t=$('rtsp').textContent;(navigator.clipboard?navigator.clipboard.writeText(t):Promise.reject()).then(()=>toast('Kopeeritud'),()=>toast(t))};
async function save(fields,msg){
 try{const d=await (await api('/api/config',form(fields))).json();
  if(!d.ok){toast(d.error||'Viga');return false}
  let t=msg||'Salvestatud';if(d.ap_forced)t+=' – hotspot jääb sisse (WiFi ja LTE on väljas)';if(d.reboot)t+=' – rakendub pärast taaskäivitust';
  toast(t);loadCfg();return true}catch(e){if(e!==0)toast('Salvestatud – ühendus võis hetkeks katkeda');return false}}
// Võrkude valik nupuloendina: <datalist> ei tööta telefonides (Android/iOS) korralikult
$('bScan').onclick=async()=>{const b=$('bScan'),box=$('ssids');b.disabled=true;b.textContent='Otsin…';
 try{const l=await (await api('/api/scan',{cache:'no-store'})).json();const best={};
  l.forEach(n=>{if(n.ssid&&(!best[n.ssid]||n.rssi>best[n.ssid].rssi))best[n.ssid]=n});
  const nets=Object.values(best).sort((a,b)=>b.rssi-a.rssi),wq=q=>q>=-55?4:q>=-65?3:q>=-75?2:q>=-85?1:0;
  box.innerHTML='';
  nets.forEach(n=>{const e=document.createElement('button');e.type='button';
   e.innerHTML=`<span></span><small>${bars(wq(n.rssi))}${n.rssi} dBm${n.enc?' 🔒':''}</small>`;e.firstChild.textContent=n.ssid;
   e.onclick=()=>{$('f_sta_ssid').value=n.ssid;box.classList.remove('show');if(n.enc)$('f_sta_pass').focus()};box.appendChild(e)});
  box.classList.toggle('show',nets.length>0);toast(nets.length?nets.length+' võrku leitud – vali loendist':'Ühtegi võrku ei leitud')}
 catch(e){if(e!==0)toast('Otsing ebaõnnestus')}
 b.disabled=false;b.textContent='Otsi võrke'};
// Võrk: WiFi + hotspot + LTE ühe salvestusega (tühi parool/PIN = muutmata)
$('fNet').onsubmit=e=>{e.preventDefault();const ap=$('f_ap_pass').value,pin=$('f_pin').value.trim();
 if(ap&&ap.length<8){showTab('tAp');toast('Hotspoti parool peab olema vähemalt 8 märki');return}
 if(pin&&!/^[0-9]{4,8}$/.test(pin)){showTab('tLte');toast('PIN peab olema 4–8 numbrit');return}
 save({sta_en:$('f_sta_en').checked?1:0,sta_ssid:$('f_sta_ssid').value,sta_pass:$('f_sta_pass').value,
  ap_en:$('f_ap_en').checked?1:0,ap_ssid:$('f_ap_ssid').value,ap_pass:ap,
  lte_en:$('f_lte_en').checked?1:0,apn:$('f_apn').value.trim(),sim_pin:pin,sim_pin_clear:$('f_pin_clear').checked?1:0},
  'Võrgu seaded salvestatud').then(o=>{if(o)['f_sta_pass','f_ap_pass','f_pin'].forEach(i=>$(i).value='')})};
function showTab(id){document.querySelectorAll('.tabs button').forEach(b=>b.classList.toggle('on',b.dataset.t==id));
 document.querySelectorAll('.tab').forEach(t=>t.hidden=t.id!=id);$('simBox').hidden=id!='tLte'}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>showTab(b.dataset.t));
$('fPw').onsubmit=async e=>{e.preventDefault();const a=$('p1').value,b=$('p2').value;
 if(a.length<4){toast('Parool peab olema vähemalt 4 märki');return}if(a!==b){toast('Paroolid ei kattu');return}
 try{const r=await api('/api/password',form({pass:a}));toast(r.ok?'Parool muudetud':'Parooli muutmine ebaõnnestus');if(r.ok){$('p1').value='';$('p2').value='';loadCfg()}}catch(e){}};
$('fSim').onsubmit=async e=>{e.preventDefault();const o=$('s_old').value.trim(),n=$('s_new').value.trim();
 if(!/^[0-9]{4,8}$/.test(o)||!/^[0-9]{4,8}$/.test(n)){toast('PIN peab olema 4–8 numbrit');return}
 if(n!==$('s_new2').value.trim()){toast('Uued PIN-id ei kattu');return}
 if(!confirm('Muuta SIM-kaardi PIN?'))return;toast('Muudan SIM PIN-i…');
 try{const d=await (await api('/api/simpin',form({old:o,new:n}))).json();
  toast(d.ok?'SIM PIN muudetud':('Ebaõnnestus: '+(d.error||'')));if(d.ok){['s_old','s_new','s_new2'].forEach(i=>$(i).value='')}}catch(e){}};
const AFS={focused:'fookuses ✓',focusing:'fokusseerin…',manual:'käsitsi',idle:'ootel',busy:'töötab','n/a':'puudub'};
let afDrag=false,afT=0;
async function afLoad(){try{const c=await (await api('/api/cam',{cache:'no-store'})).json();
 if(document.activeElement!==$('f_mirror'))$('f_mirror').checked=!!c.hmirror;
 $('afBox').style.display=c.af?'':'none';if(!c.af)return;
 if(document.activeElement!==$('f_af_mode'))$('f_af_mode').value=c.af_mode;$('afMan').style.display=c.af_mode==2?'':'none';
 if(!afDrag){$('f_af_pos').value=c.af_pos}
 $('af_pos_t').textContent=$('f_af_pos').value;
 $('af_st').textContent=(AFS[c.af_state]||c.af_state)+(c.lens>=0?` · lääts ${c.lens}`:'')}catch(e){}}
$('f_mirror').onchange=async e=>{const r=await api('/api/cam?var=hmirror&val='+(e.target.checked?1:0));toast(r.ok?(e.target.checked?'Peegeldus sees':'Peegeldus väljas'):'Ebaõnnestus')};
$('f_af_mode').onchange=async e=>{await api('/api/cam?var=af_mode&val='+e.target.value);toast('Fookuse režiim muudetud');setTimeout(afLoad,300)};
$('f_af_pos').oninput=e=>{afDrag=true;$('af_pos_t').textContent=e.target.value;const n=Date.now();if(n-afT>150){afT=n;api('/api/cam?var=af_pos_live&val='+e.target.value).catch(()=>{})}};
$('f_af_pos').onchange=async e=>{afDrag=false;await api('/api/cam?var=af_pos&val='+e.target.value);afLoad()};
$('bAfNow').onclick=async()=>{const b=$('bAfNow');b.disabled=true;try{const r=await api('/api/focus',{method:'POST'});toast(r.ok?'Fokusseerin…':'Autofookus pole saadaval')}catch(e){}
 let k=0;const t=setInterval(()=>{afLoad();if(++k>12){clearInterval(t);b.disabled=false}},500)};
afLoad();setInterval(afLoad,5000);
const PST={off:'väljas',connecting:'ühendan…',streaming:'● saadab',retry:'uus katse varsti',error:'viga'};
function showPush(p){if(!p)return;const up=p.up_s,h=Math.floor(up/3600),m=Math.floor(up%3600/60);
 $('push_st').textContent=(PST[p.state]||p.state)+(p.state=='streaming'?` · ${h?h+' h ':''}${m} min`:'')+(p.retries?` · katkestusi ${p.retries}`:'');
 $('push_st').style.color=p.state=='streaming'?'var(--ok)':p.state=='retry'||p.state=='error'?'var(--warn)':'';
 $('push_rate').textContent=p.state=='streaming'?`${p.fps.toFixed(1)} fps · ${p.kBps.toFixed(0)} kB/s · kokku ${p.sent_mb.toFixed(1)} MB`:'-';
 $('push_err').textContent=p.error&&p.state!='streaming'?'⚠ '+p.error:''}
// Voogedastus: RTSP parool + push ühe salvestusega
const pushVis=()=>$('pushBox').style.display=$('f_push_en').checked?'':'none';
$('f_push_en').onchange=pushVis;
$('fStream').onsubmit=e=>{e.preventDefault();const u=$('f_push_url').value.trim(),on=$('f_push_en').checked;
 if(on&&!u){toast('Sisesta serveri aadress');return}
 if(u&&!/^rtsp:\/\//i.test(u)){toast('Aadress peab algama rtsp://');return}
 const f={rtsp_auth:$('f_rtsp_auth').checked?1:0,push_en:on?1:0,push_url:u,push_user:$('f_push_user').value.trim()};
 if($('f_push_pass').value)f.push_pass=$('f_push_pass').value;
 save(f,'Voogedastuse seaded salvestatud')};
// Kaamera: nimi + resolutsioon (resolutsioon saadetakse ainult muutumisel – see taaskäivitab sensori)
let curFs=null;
$('fCamAll').onsubmit=async e=>{e.preventDefault();const v=$('f_cam_name').value.trim(),f={cam_name:v};
 if(curFs!==null&&$('f_fs').value!=curFs)f.framesize=$('f_fs').value;
 if(await save(f,'Kaamera seaded salvestatud')){const n=v||'SimCam';document.title=n+' – seaded';document.querySelector('header h1').textContent=n+' – seaded'}};
// Rühmad kokkuvolditavad: telefonis vaikimisi avatud ainult Kaamera ja Võrk; valik jääb meelde
(()=>{let st={};try{st=JSON.parse(localStorage.getItem('simcam_grp')||'{}')}catch(e){}
 const narrow=matchMedia('(max-width:700px)').matches;
 document.querySelectorAll('.grp').forEach(c=>{const d=c.firstElementChild,g=c.dataset.g;
  d.open=g in st?st[g]:(!narrow||g=='cam'||g=='net');
  d.addEventListener('toggle',()=>{st[g]=d.open;try{localStorage.setItem('simcam_grp',JSON.stringify(st))}catch(e){}})})})();
$('f_mic_gain').oninput=()=>$('m_gain_t').textContent=$('f_mic_gain').value+' dB';
$('f_mic_en').onchange=()=>{const on=$('f_mic_en').checked;$('micOn').style.display=on?'':'none';if(!on)SimAudio.stop();
 save({mic_en:on?1:0},on?'Mikrofon sees':'Mikrofon väljas')};
$('fMic').onsubmit=e=>{e.preventDefault();SimAudio.stop();
 save({mic_gain:$('f_mic_gain').value,mic_codec:$('f_mic_codec').value,rtsp_audio:$('f_rtsp_audio').checked?1:0},'Mikrofoni seaded salvestatud')};
SimAudio.onstate=on=>{$('bListen').textContent=on?'⏹ Lõpeta':'🔈 Kuula'};SimAudio.onerror=m=>toast('Heli: '+m);
$('bListen').onclick=()=>SimAudio.active()?SimAudio.stop():SimAudio.start();
function showMic(m){if(!m)return;const pct=m.running?Math.max(0,Math.min(100,(m.level+70)/70*100)):0;
 $('m_lvl_b').style.width=pct+'%';$('m_lvl').classList.toggle('hot',m.peak>-3);
 $('m_lvl_t').textContent=m.running?`${m.level.toFixed(0)} dBFS (tipp ${m.peak.toFixed(0)})`:(m.enabled?'ootel – töötab ainult pildi vaatamise või kuulamise ajal':'-')}
$('bReboot').onclick=async()=>{if(!confirm('Kas taaskäivitada seade?'))return;try{await api('/api/reboot',{method:'POST'})}catch(e){}toast('Taaskäivitan…')};
const OST={idle:'-',checking:'Kontrollin…',uptodate:'Ajakohane ✓',available:'Uuendus saadaval!',updating:'Uuendan…',done:'Paigaldatud – taaskäivitub',error:'Viga'};
async function otaPoll(){try{const o=await (await api('/api/ota',{cache:'no-store'})).json();
 $('o_cur').textContent=o.current;$('o_lat').textContent=(o.latest||'-')+(o.skip&&o.skip==o.latest?' (tagasi pööratud – auto jätab vahele)':'');
 if(document.activeElement!==$('f_auto'))$('f_auto').checked=!!o.auto;
 let st=OST[o.state]||o.state;if(o.state=='error')st+=': '+o.error;if(o.state=='updating')st+=' '+o.progress+'%';
 if(o.checked_ago&&(o.state=='uptodate'||o.state=='available'))st+=` (${Math.round(o.checked_ago/60)} min tagasi)`;
 $('o_st').textContent=st;$('bOtaUpd').disabled=o.state!='available';$('bOtaChk').disabled=o.state=='checking'||o.state=='updating';
 $('o_barw').style.display=(o.state=='updating'||o.state=='done')?'block':'none';$('o_bar').style.width=o.progress+'%';
 setTimeout(otaPoll,(o.state=='updating'||o.state=='checking')?1000:5000)}catch(e){setTimeout(otaPoll,5000)}}
$('f_auto').onchange=async()=>{const on=$('f_auto').checked;
 if(on&&!confirm('Lülitada sisse automaatne uuendamine? Seade paigaldab uued versioonid GitHubist ise ja taaskäivitub.')){$('f_auto').checked=false;return}
 await save({auto_update:on?1:0},on?'Automaatne uuendamine sees – kontrollin kohe':'Automaatne uuendamine väljas');otaPoll()};
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
  $('c_af').textContent=c.af;$('c_rot').textContent=c.rotate+'°';
  $('c_cli').textContent=s.rtsp_clients+' / '+s.http_streams;
  $('s_up').textContent=dur(s.uptime);$('s_ram').textContent=kb(s.heap_free)+' / '+kb(s.heap_total);
  $('s_ps').textContent=kb(s.psram_free)+' / '+kb(s.psram_total);$('s_t').textContent=s.temp.toFixed(1)+' °C';$('s_fw').textContent=s.fw;
  showBat(d.bat);showMic(d.mic);showPush(d.push);$('s_rst').textContent=s.reset;$('s_log').textContent=`${s.log_w} hoiatust, ${s.log_e} viga`;
 }catch(e){if(e!==0)$('hdr').innerHTML='<span class="dot bad"></span>Seade ei vasta'}
 setTimeout(poll,3000)}
const BST={MEASURING:'Mõõdan…',ABSENT:'Aku puudub',CHARGING:'⚡ Laeb',FULL:'✓ Täis / laadijal',DISCHARGING:'Tühjeneb',LOW:'⚠ Madal – laadi!',STABLE:'Stabiilne'};
let bHist=[],bStep=30,bRange=24,bTimer=0;
function hm(m){return m>=60?Math.floor(m/60)+' h '+(m%60)+' min':m+' min'}
function showBat(b){if(!b)return;$('batOn').style.display=b.enabled?'':'none';$('batOff').style.display=b.enabled?'none':'';
 if(document.activeElement!==$('f_bat_en'))$('f_bat_en').checked=b.enabled;$('b_cal').textContent='×'+b.cal.toFixed(4);
 const absent=b.state=='ABSENT';$('b_pct').textContent=absent?'–':b.pct+' %';$('b_st').textContent=BST[b.state]||b.state;
 $('b_bar').style.width=(absent?0:b.pct)+'%';$('b_meter').className='meter'+(b.pct<=15?' low':b.pct<=30?' mid':'');
 $('b_v').textContent=b.v.toFixed(2)+' V';
 $('b_sl').textContent=b.state=='MEASURING'?'-':(b.slope>0?'+':'')+b.slope.toFixed(1)+' mV/min';
 $('b_left').textContent=b.min_left>=0?'~'+hm(b.min_left)+' (hinnang)':b.state=='CHARGING'?'laeb':b.state=='FULL'?'täis / laadijal':
  b.state=='ABSENT'?'-':b.hist_min<20?`arvutan… (~${20-b.hist_min} min)`:'aku ei tühjene märgatavalt';
 $('b_usb').textContent=b.usb?'jah (laeb USB-st)':'ei / ainult laadija'}
function drawBat(){const svg=$('b_svg'),W=svg.clientWidth||300,H=150,L=38,R=6,T=8,B=20;
 const n=Math.min(bHist.length,Math.round(bRange*3600/bStep)),d=bHist.slice(-n);
 if(d.length<2){svg.innerHTML=`<text x="${W/2}" y="${H/2}" text-anchor="middle" font-size="12" fill="var(--mut)">Ajalugu koguneb (punkt iga ${bStep} s)</text>`;return}
 let lo=Math.min(...d),hi=Math.max(...d);if(hi-lo<100){const m=(hi+lo)/2;lo=m-50;hi=m+50}const pad=(hi-lo)*.08;lo-=pad;hi+=pad;
 const span=bRange*3600,x=i=>L+(W-L-R)*(1-((d.length-1-i)*bStep)/span),y=v=>T+(H-T-B)*(1-(v-lo)/(hi-lo));
 let g='';for(let k=0;k<=3;k++){const v=lo+(hi-lo)*k/3,yy=y(v).toFixed(1);
  g+=`<line x1="${L}" x2="${W-R}" y1="${yy}" y2="${yy}" stroke="var(--line)" stroke-width="1"/><text x="${L-5}" y="${+yy+4}" text-anchor="end" font-size="11" fill="var(--mut)">${(v/1000).toFixed(2)}</text>`}
 [[0,'-'+bRange+' h'],[.5,bRange>1?'-'+(bRange/2)+' h':'-30 min'],[1,'nüüd']].forEach(([f,t])=>{g+=`<text x="${(L+(W-L-R)*f).toFixed(1)}" y="${H-5}" text-anchor="${f==0?'start':f==1?'end':'middle'}" font-size="11" fill="var(--mut)">${t}</text>`});
 const pts=d.map((v,i)=>x(i).toFixed(1)+','+y(v).toFixed(1)).join(' ');
 g+=`<polyline points="${pts}" fill="none" stroke="var(--acc)" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>`;
 g+=`<line id="b_x" y1="${T}" y2="${H-B}" stroke="var(--mut)" stroke-width="1" style="display:none"/><circle id="b_dot" r="4" fill="var(--acc)" stroke="var(--card)" stroke-width="2" style="display:none"/>`;
 svg.innerHTML=g;
 svg.onpointermove=e=>{const r=svg.getBoundingClientRect(),px=e.clientX-r.left;let i=Math.round(d.length-1-(1-(px-L)/(W-L-R))*span/bStep);
  i=Math.max(0,Math.min(d.length-1,i));const cx=x(i),cy=y(d[i]),ago=Math.round((d.length-1-i)*bStep/60);
  const xl=$('b_x'),dot=$('b_dot'),tip=$('b_tip');xl.setAttribute('x1',cx);xl.setAttribute('x2',cx);xl.style.display='';dot.setAttribute('cx',cx);dot.setAttribute('cy',cy);dot.style.display='';
  tip.style.display='block';tip.style.left=Math.max(50,Math.min(W-50,cx))+'px';tip.textContent=(d[i]/1000).toFixed(2)+' V · '+(ago?hm(ago)+' tagasi':'nüüd')};
 svg.onpointerleave=()=>{['b_x','b_dot'].forEach(i=>$(i)&&($(i).style.display='none'));$('b_tip').style.display='none'}}
async function batPoll(){clearTimeout(bTimer);try{const b=await (await api('/api/battery',{cache:'no-store'})).json();bHist=b.hist;bStep=b.step_s;showBat(b.bat);drawBat()}catch(e){}
 bTimer=setTimeout(batPoll,30000)}
$('b_rng').onclick=e=>{const h=+e.target.dataset.h;if(!h)return;bRange=h;document.querySelectorAll('#b_rng button').forEach(b=>b.classList.toggle('on',+b.dataset.h===h));drawBat()};
window.addEventListener('resize',drawBat);
$('b_graph').addEventListener('toggle',()=>{if($('b_graph').open)drawBat()});
async function batSave(f,msg){try{const d=await (await api('/api/battery',form(f))).json();toast(d.ok?msg:(d.error||'Viga'));if(d.ok)batPoll();return d.ok}catch(e){}}
$('f_bat_en').onchange=()=>batSave({en:$('f_bat_en').checked?1:0},$('f_bat_en').checked?'Aku jälgimine sees':'Aku jälgimine väljas');
$('fBat').onsubmit=e=>{e.preventDefault();const v=$('f_bat_v').value.trim();if(!v){toast('Sisesta mõõdetud pinge');return}
 batSave({v},'Kalibreeritud').then(o=>{if(o)$('f_bat_v').value=''})};
$('bCalReset').onclick=e=>{e.preventDefault();batSave({reset_cal:1},'Kalibreering lähtestatud')};
loadCfg();poll();otaPoll();batPoll();
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
//  Sisselogimine
// -----------------------------------------------------------------------------
static const char LOGIN_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="et"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{NAME}} – sisselogimine</title><link rel="stylesheet" href="/style.css">
<style>
body{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.box{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:22px;width:100%;max-width:340px;display:grid;gap:12px}
h1{margin:0;font-size:20px}.err{color:var(--bad);font-size:14px;min-height:1em}
</style></head><body>
<form class="box" method="post" action="/login">
 <h1>{{NAME}}</h1>
 <input type="password" name="pass" placeholder="Parool" autofocus autocomplete="current-password" required>
 <span class="err" id="err"></span>
 <button class="pri" type="submit">Logi sisse</button>
</form>
<script>if(location.search.indexOf('e=1')>=0)document.getElementById('err').textContent='Vale parool';</script>
</body></html>)HTML";

// -----------------------------------------------------------------------------
//  Logi (jooksev + eelmine käivitus)
// -----------------------------------------------------------------------------
static const char LOG_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="et"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{NAME}} – logi</title><link rel="stylesheet" href="/style.css">
<style>
body{display:flex;flex-direction:column;height:100vh;height:100dvh}
header{display:flex;flex-wrap:wrap;align-items:center;gap:8px;padding:10px 16px;border-bottom:1px solid var(--line);background:var(--card)}
header h1{font-size:18px;margin:0 8px 0 0}
.sp{flex:1}
select{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:10px;padding:8px 10px}
input[type=search]{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:10px;padding:8px 10px;max-width:180px;min-width:0;flex:1}
#info{font-size:13px;color:var(--mut);padding:6px 16px;border-bottom:1px solid var(--line)}
#log{flex:1;overflow:auto;margin:0;padding:10px 16px;font:12.5px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere}
#log .W{color:var(--warn)}#log .E{color:var(--bad);font-weight:600}#log .t{color:var(--mut)}
button.on{background:var(--acc);color:#fff;border-color:var(--acc)}
</style></head><body>
<header><a class="btn" href="/settings">← Seaded</a><h1>{{NAME}} – logi</h1>
 <select id="src"><option value="cur">See käivitus</option><option value="prev">Eelmine käivitus</option></select>
 <select id="lvl"><option value="">Kõik</option><option value="WE">Hoiatused + vead</option><option value="E">Ainult vead</option></select>
 <input type="search" id="q" placeholder="Filtreeri…">
 <span class="sp"></span>
 <button id="bClock" title="Näita kellaaega (brauseri kellast)">🕒</button>
 <button id="bPause">⏸ Peata</button>
 <a class="btn" id="bDl" href="/api/log?dl=1">⬇ Laadi alla</a>
 <button class="dng" id="bClr">Tühjenda</button>
</header>
<div id="info">…</div>
<pre id="log"></pre>
<div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),2500)}
async function api(u,o){const r=await fetch(u,o);if(r.status==401){location.href='/login';throw 0}return r}
let lines=[],pos=0,paused=false,clock=false,up0=0,t0=0,timer=0;
const esc=s=>s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const RE=/^\[\s*(\d+)\]\[([IWE])\]/;
function stamp(ms){if(!clock||!up0)return null;const d=new Date(t0-(up0-ms));
 return d.toLocaleDateString('et-EE',{day:'2-digit',month:'2-digit'})+' '+d.toLocaleTimeString('et-EE')}
function fmt(l){const m=RE.exec(l);let h=esc(l);
 if(m){const s=stamp(+m[1]);if(s)h=`<span class="t">${s}</span> `+h;return `<span class="${m[2]}">${h}</span>`}return h}
function render(){const lv=$('lvl').value,q=$('q').value.toLowerCase(),box=$('log');
 const atEnd=box.scrollHeight-box.scrollTop-box.clientHeight<40;
 box.innerHTML=lines.filter(l=>{if(lv){const m=RE.exec(l);if(!m||lv.indexOf(m[2])<0)return false}return !q||l.toLowerCase().includes(q)}).map(fmt).join('\n');
 if(atEnd||!paused)box.scrollTop=box.scrollHeight}
const RST={POWERON:'toide sisse',EXT:'väline reset',SW:'tarkvaraline (taaskäivitus/uuendus)',PANIC:'KOKKUJOOKSMINE (panic)',
 INT_WDT:'KOKKUJOOKSMINE (katkestuse watchdog)',TASK_WDT:'KOKKUJOOKSMINE (taski watchdog)',WDT:'KOKKUJOOKSMINE (watchdog)',
 DEEPSLEEP:'ärkamine süvaunest',BROWNOUT:'TOITEPINGE LANGUS (brownout)',OTHER:'teadmata'};
function info(r,extra){const w=r.headers.get('X-Log-Warn'),e=r.headers.get('X-Log-Err');
 $('info').textContent=`Taaskäivituse põhjus: ${RST[r.headers.get('X-Reset-Reason')]||'-'} · hoiatusi ${w||0}, vigu ${e||0}`+(extra||'')}
async function load(){clearTimeout(timer);const prev=$('src').value=='prev';
 try{const r=await api(prev?'/api/log?prev=1':'/api/log?since='+pos,{cache:'no-store'});
  const up=+r.headers.get('X-Uptime-Ms');if(up){up0=up;t0=Date.now()}
  const txt=await r.text();
  if(prev){lines=txt?txt.replace(/\n$/,'').split('\n'):[];info(r,' · eelmise käivituse viimased read (alles ainult tarkvaralise taaskäivituse/krahhi järel)');
   if(!txt)$('log').textContent='Eelmise käivituse logi pole (seade sai vahepeal toite).';else render();return}
  const nx=+r.headers.get('X-Log-Next');if(nx<pos){lines=[]}pos=nx;
  if(txt){lines=lines.concat(txt.replace(/\n$/,'').split('\n'));if(lines.length>3000)lines=lines.slice(-3000);render()}
  info(r)}catch(e){if(e!==0)$('info').textContent='Seade ei vasta'}
 if(!paused)timer=setTimeout(load,2000)}
$('src').onchange=()=>{lines=[];pos=0;$('log').textContent='';
 $('bDl').href=$('src').value=='prev'?'/api/log?prev=1&dl=1':'/api/log?dl=1';load()};
$('lvl').onchange=render;$('q').oninput=render;
$('bClock').onclick=()=>{clock=!clock;$('bClock').classList.toggle('on',clock);render()};
$('bPause').onclick=()=>{paused=!paused;$('bPause').textContent=paused?'▶ Jätka':'⏸ Peata';if(!paused)load()};
$('bClr').onclick=async()=>{if(!confirm('Tühjendada jooksev logi?'))return;
 try{await api('/api/log/clear',{method:'POST'});lines=[];$('log').textContent='';toast('Logi tühjendatud');load()}catch(e){}};
load();
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
//  Helimängija (/audio.js) – loeb /audio voogu fetch-iga ja mängib Web Audio-ga
//  ~0,15–0,3 s viivitusega. SimAudio.start()/stop(); onstate(bool) tagasiside.
// -----------------------------------------------------------------------------
static const char AUDIO_JS[] PROGMEM = R"JS(
window.SimAudio=(()=>{let ctx=null,ctl=null,next=0,on=false;const A={onstate:null,onerror:null};
const UL=new Float32Array(256);for(let i=0;i<256;i++){const u=~i&255,e=(u>>4)&7,m=u&15;let x=(((m<<3)+0x84)<<e)-0x84;UL[i]=(u&0x80?-x:x)/32768}
function play(f,rate){if(!f.length)return;const b=ctx.createBuffer(1,f.length,rate);b.copyToChannel(f,0);
 const s=ctx.createBufferSource();s.buffer=b;s.connect(ctx.destination);const now=ctx.currentTime;
 if(next<now+0.04||next>now+0.6)next=now+0.15;s.start(next);next+=b.duration}
function set(v){on=v;A.onstate&&A.onstate(v)}
A.start=async()=>{if(on)return;try{ctx=ctx||new (window.AudioContext||window.webkitAudioContext)();await ctx.resume();
 ctl=new AbortController();set(true);next=0;
 const r=await fetch('/audio',{signal:ctl.signal,cache:'no-store'});
 if(r.status==401){location.href='/login';return}
 if(!r.ok)throw new Error(await r.text()||('HTTP '+r.status));
 const l16=r.headers.get('X-Audio-Format')=='s16le',rate=+r.headers.get('X-Audio-Rate')||8000,rd=r.body.getReader();let carry=null;
 for(;;){const {value,done}=await rd.read();if(done)break;let b=value;
  if(l16){if(carry){const t=new Uint8Array(carry.length+b.length);t.set(carry);t.set(b,carry.length);b=t;carry=null}
   if(b.length&1){carry=b.slice(-1);b=b.slice(0,-1)}const dv=new DataView(b.buffer,b.byteOffset,b.length),f=new Float32Array(b.length/2);
   for(let i=0;i<f.length;i++)f[i]=dv.getInt16(2*i,true)/32768;play(f,rate)}
  else{const f=new Float32Array(b.length);for(let i=0;i<b.length;i++)f[i]=UL[b[i]];play(f,rate)}}
 if(on)throw new Error('Heli voog katkes')}catch(e){if(on&&e.name!=='AbortError')A.onerror&&A.onerror(e.message||String(e))}
 finally{if(ctl)ctl.abort();ctl=null;set(false)}};
A.stop=()=>{if(ctl)ctl.abort();set(false)};A.active=()=>on;return A})();
)JS";
