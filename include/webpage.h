// The settings page served at "/". Self-contained: no external assets, so it
// works without internet access. Talks to /api/config and /api/status.
#pragma once

#include <pgmspace.h>

static const char kSettingsPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bedside Clock</title>
<style>
:root{--bg:#000;--card:#111214;--line:#26272b;--text:#F3F4F6;--dim:#9CA3AF;--faint:#6B7280;
--green:#4ADE80;--amber:#FBBF24;--red:#F87171}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.4 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px 16px 96px}
header{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin:8px 0 20px}
h1{font-size:22px;margin:0;font-weight:600}
#now{color:var(--dim);font-size:15px;text-align:right}
#now b{font-size:20px}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}
h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--dim);margin:0 0 12px;font-weight:600}
label{display:block;margin:12px 0 4px;color:var(--dim);font-size:14px}
label:first-of-type{margin-top:0}
input,select{width:100%;background:#000;color:var(--text);border:1px solid var(--line);border-radius:8px;
padding:10px 12px;font:inherit;min-height:44px}
input:focus,select:focus{outline:2px solid var(--green);outline-offset:-1px;border-color:transparent}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}
.row>div,.row3>div{min-width:0}
.hint{color:var(--faint);font-size:13px;margin:6px 0 0}
.check{display:flex;align-items:center;gap:8px;margin-top:10px;color:var(--dim);font-size:14px}
.check input{width:auto;min-height:0}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px;vertical-align:middle}
.wifi{display:grid;grid-template-columns:1fr 1fr auto;gap:8px;margin-bottom:8px;align-items:center}
.switch{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:16px 0 4px;cursor:pointer;color:var(--text);font-size:16px}
.switch b{font-weight:500}
.switch input{position:absolute;opacity:0;width:1px;height:1px;min-height:0}
.switch i{flex:none;width:48px;height:28px;border-radius:14px;background:#2a2b2f;position:relative;transition:background .15s}
.switch i::after{content:"";position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:50%;background:#d1d5db;transition:transform .15s}
.switch input:checked+i{background:var(--green)}
.switch input:checked+i::after{transform:translateX(20px);background:#000}
.switch input:focus-visible+i{outline:2px solid var(--green);outline-offset:2px}
fieldset{border:0;padding:0;margin:0;min-width:0}
fieldset:disabled{opacity:.4}
button{font:inherit;border-radius:8px;border:1px solid var(--line);background:#1a1b1e;color:var(--text);
padding:10px 14px;min-height:44px;cursor:pointer}
button.remove{padding:0 12px;color:var(--red)}
#bar{position:fixed;left:0;right:0;bottom:0;background:rgba(0,0,0,.92);border-top:1px solid var(--line);padding:12px 16px}
#bar div{max-width:560px;margin:0 auto;display:flex;align-items:center;gap:12px}
#save{background:var(--green);color:#000;border:0;font-weight:600;flex:none;padding:10px 24px}
#save:disabled{opacity:.5}
#msg{color:var(--dim);font-size:14px;flex:1}
#msg.err{color:var(--red)}#msg.ok{color:var(--green)}
@media (max-width:420px){.row3{grid-template-columns:1fr 1fr}.wifi{grid-template-columns:1fr auto}.wifi .pw{grid-column:1}}
</style></head><body><main>
<header><h1>Bedside Clock</h1><div id="now">&nbsp;</div></header>

<form id="f" autocomplete="off" onsubmit="return false">
<section><h2>Nightscout</h2>
<label for="nightscout">Site</label>
<input id="nightscout" placeholder="cgm.example.com" spellcheck="false" autocapitalize="off">
<label for="token">Access token</label>
<input id="token" type="password" spellcheck="false" autocapitalize="off">
<label class="check"><input type="checkbox" id="clearToken"> Remove the token</label>
<p class="hint">Changing the site or token restarts the clock.</p>
</section>

<section><h2>Glucose</h2>
<label for="unit">Units</label>
<select id="unit"><option value="0">mmol/L</option><option value="1">mg/dL</option></select>
<div class="row" style="margin-top:12px">
<div><label for="redLow"><span class="dot" style="background:var(--red)"></span>Urgent below</label><input id="redLow" type="number" step="any" inputmode="decimal"></div>
<div><label for="redHigh"><span class="dot" style="background:var(--red)"></span>Urgent above</label><input id="redHigh" type="number" step="any" inputmode="decimal"></div>
<div><label for="yellowLow"><span class="dot" style="background:var(--amber)"></span>Warn below</label><input id="yellowLow" type="number" step="any" inputmode="decimal"></div>
<div><label for="yellowHigh"><span class="dot" style="background:var(--amber)"></span>Warn above</label><input id="yellowHigh" type="number" step="any" inputmode="decimal"></div>
</div>
<p class="hint">Green between the warn values. Switching units converts these for you.</p>
<label for="stale">Grey out readings older than (minutes)</label>
<input id="stale" type="number" min="1" max="120" inputmode="numeric">
</section>

<section><h2>Brightness</h2>
<p class="hint" style="margin:0 0 4px">Button A cycles these three levels. The clock remembers the one you pick.</p>
<div class="row3">
<div><label for="b1">Level 1 %</label><input id="b1" type="number" min="1" max="100" inputmode="numeric"></div>
<div><label for="b2">Level 2 %</label><input id="b2" type="number" min="1" max="100" inputmode="numeric"></div>
<div><label for="b3">Level 3 %</label><input id="b3" type="number" min="1" max="100" inputmode="numeric"></div>
</div>
<label class="switch"><span><b>Dim at night</b><br><span class="hint">Off: the level you pick stays put, day and night.</span></span>
<input type="checkbox" id="nightMode" role="switch"><i></i></label>
<fieldset id="night"><div class="row3">
<div><label for="nightStart">Night from</label><input id="nightStart" type="time"></div>
<div><label for="nightEnd">Until</label><input id="nightEnd" type="time"></div>
<div><label for="nightB">Night %</label><input id="nightB" type="number" min="0" max="100" inputmode="numeric"></div>
</div>
<p class="hint">Between these times the screen dims to the night level; any button brings it back for 30 s.</p></fieldset>
</section>

<section><h2>Clock</h2>
<div class="row">
<div><label for="timeFormat">Time</label><select id="timeFormat"><option value="0">24 hour</option><option value="1">12 hour</option></select></div>
<div><label for="dateFormat">Date</label><select id="dateFormat"><option value="0">Wed 14 Oct</option><option value="1">Wed Oct 14</option></select></div>
</div>
<label for="tzPreset">Time zone</label>
<select id="tzPreset">
<option value="GMT0BST,M3.5.0/1,M10.5.0">UK (GMT/BST)</option>
<option value="GMT0IST,M3.5.0/1,M10.5.0">Ireland</option>
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe</option>
<option value="EST5EDT,M3.2.0,M11.1.0">US Eastern</option>
<option value="CST6CDT,M3.2.0,M11.1.0">US Central</option>
<option value="MST7MDT,M3.2.0,M11.1.0">US Mountain</option>
<option value="PST8PDT,M3.2.0,M11.1.0">US Pacific</option>
<option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Sydney</option>
<option value="NZST-12NZDT,M9.5.0,M4.1.0/3">New Zealand</option>
<option value="">Custom…</option>
</select>
<input id="tz" style="margin-top:8px" spellcheck="false" autocapitalize="off" placeholder="POSIX TZ string">
</section>

<section><h2>Wi-Fi</h2>
<div id="wifi"></div>
<button type="button" id="addWifi">Add network</button>
<p class="hint">Tried in turn; the strongest one in range wins. Leave a password blank to keep it. Changes restart the clock.</p>
</section>
</form>
</main>
<div id="bar"><div><span id="msg"></span><button id="save" type="button">Save</button></div></div>

<script>
const $=id=>document.getElementById(id);
let cfg=null;
const thr=['redLow','yellowLow','yellowHigh','redHigh'];

function wifiRow(w){
  const d=document.createElement('div');d.className='wifi';d.dataset.orig=w.orig;
  d.innerHTML='<input class="ssid" placeholder="Network name" spellcheck="false" autocapitalize="off">'+
    '<input class="pw" type="password" placeholder="'+(w.hasPass?'Password unchanged':'Password')+'">'+
    '<button type="button" class="remove" aria-label="Remove network">✕</button>';
  d.querySelector('.ssid').value=w.ssid;
  d.querySelector('.remove').onclick=()=>{d.remove();addState()};
  $('wifi').appendChild(d);addState();
}
function addState(){$('addWifi').disabled=$('wifi').children.length>=10}

function syncTz(){const o=[...$('tzPreset').options].find(o=>o.value===$('tz').value);
  $('tzPreset').value=o?o.value:'';$('tz').style.display=o?'none':'block'}

async function load(){
  cfg=await (await fetch('/api/config')).json();
  $('nightscout').value=cfg.nightscout;
  $('token').placeholder=cfg.tokenSet?'Token set — leave blank to keep':'No token';
  $('unit').value=cfg.showMgdl?1:0;
  thr.forEach(k=>$(k).value=cfg.showMgdl?cfg[k]:Number(cfg[k]).toFixed(1));
  $('stale').value=cfg.staleMinutes;
  ['b1','b2','b3'].forEach((k,i)=>$(k).value=cfg.brightness[i]);
  $('nightMode').checked=cfg.nightMode;$('night').disabled=!cfg.nightMode;$('nightStart').value=cfg.nightStart;$('nightEnd').value=cfg.nightEnd;$('nightB').value=cfg.nightBrightness;
  $('timeFormat').value=cfg.timeFormat;$('dateFormat').value=cfg.dateFormat;
  $('tz').value=cfg.tz;syncTz();
  $('wifi').innerHTML='';cfg.wifi.forEach(wifiRow);
}

async function status(){
  try{const s=await (await fetch('/api/status')).json();
    const c={InRange:'var(--green)',Warning:'var(--amber)',Urgent:'var(--red)'}[s.range];
    $('now').innerHTML=s.hasBg?'<b style="color:'+(s.stale?'var(--faint)':c)+'">'+s.bg+'</b> '+s.delta+
      '<br>'+s.time+' · '+s.age:s.time;
  }catch(e){}
}

$('unit').onchange=()=>{const toMg=$('unit').value==='1';
  thr.forEach(k=>{const v=parseFloat($(k).value);if(!isNaN(v))$(k).value=toMg?Math.round(v*18):(v/18).toFixed(1)})};
$('tzPreset').onchange=()=>{if($('tzPreset').value){$('tz').value=$('tzPreset').value;$('tz').style.display='none'}
  else{$('tz').style.display='block';$('tz').focus()}};
$('nightMode').onchange=()=>$('night').disabled=!$('nightMode').checked;
$('addWifi').onclick=()=>wifiRow({ssid:'',orig:-1,hasPass:false});

function msg(t,cls){$('msg').textContent=t;$('msg').className=cls||''}

$('save').onclick=async()=>{
  const body={nightscout:$('nightscout').value.trim(),token:$('token').value.trim(),clearToken:$('clearToken').checked,
    showMgdl:$('unit').value==='1',staleMinutes:+$('stale').value,
    brightness:['b1','b2','b3'].map(k=>+$(k).value),nightMode:$('nightMode').checked,nightBrightness:+$('nightB').value,
    nightStart:$('nightStart').value,nightEnd:$('nightEnd').value,
    timeFormat:+$('timeFormat').value,dateFormat:+$('dateFormat').value,tz:$('tz').value.trim(),
    wifi:[...$('wifi').children].map(d=>({ssid:d.querySelector('.ssid').value.trim(),
      pass:d.querySelector('.pw').value,orig:+d.dataset.orig})).filter(w=>w.ssid)};
  thr.forEach(k=>body[k]=parseFloat($(k).value));
  $('save').disabled=true;msg('Saving…');
  try{
    const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const j=await r.json();
    if(!r.ok||!j.ok){msg(j.error||'Could not save','err');$('save').disabled=false;return}
    if(j.restart){msg('Saved. Restarting the clock…','ok');setTimeout(()=>location.reload(),15000)}
    else{msg('Saved and applied','ok');$('token').value='';$('clearToken').checked=false;await load();$('save').disabled=false}
  }catch(e){msg('Clock not reachable','err');$('save').disabled=false}
};

load().catch(()=>msg('Could not load settings','err'));
status();setInterval(status,10000);
</script></body></html>)HTML";
