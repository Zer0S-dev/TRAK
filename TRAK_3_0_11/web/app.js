/* =========================================================
   TRAK REAL DATA — /api/data
   ========================================================= */

let trackerMap=null;
let trackerMarker=null;
let mapFollow=true;
let lastTrackerPosition=null;
let lastData=null;
let refreshBusy=false;
let sentinelUiState=null;
let sentinelUiDeadline=0;
let sentinelUiTimer=null;
let sentinelUiOptimistic=false;

/* =========================================================
   DEV MODE — DONNEES DE TEST UI
   =========================================================
   true  = le dashboard utilise DEV_DATA au lieu de /api/data
   false = fonctionnement normal avec le firmware
*/
const DEV_MODE = true;

const DEV_DATA = {
  latitude: 49.721500,
  longitude: 0.786200,
  altitude: 142,
  speedKmh: 67,
  hasFix: true,
  satellites: 12,
  gpsSatellites: 7,
  glonassSatellites: 3,
  beidouSatellites: 1,
  galileoSatellites: 1,

  network: '4G',
  internetAvailable: true,
  signalPercent: 78,

  wifiBytes: 18400000,
  cellularBytes: 428000000,
  estimated4GBytes: 470800000,
  totalBytes: 446400000,
  dataPlanMb: 5000,
  dataCoefficient: 1.10,

  txCount: 1284,
  firmwareVersion: '3.0.9',

  motionMode: 'IMMOBILE',
  motionDps: 0.0,
  motionXDps: 0.0,
  motionYDps: 0.0,
  motionZDps: 0.0,
  motionStationaryConfirmationRemainingMs: 0,
  stationaryConfirmed: true,

  sentinelState: 'OFF',
  phoneConfigured: true,
  sentinelPhoneConfigured: true,
  trakPhoneConfigured: true,
  communicationReady: true,
  phone: '+33 6 12 34 56 78',
  trakPhone: '+33 7 12 34 56 78',
  sentinelConfirmationRemainingMs: 0,

  wifiProfiles: [
    {slot:1, ssid:'Livebox-TRAK'},
    {slot:2, ssid:'Garage-WiFi'},
    {slot:3, ssid:''}
  ]
};

const $=id=>document.getElementById(id);

/* =========================================================
   RETOUR TACTILE — APPLICATION ANDROID
   =========================================================
   La vibration est désormais exécutée nativement par Android.
   Firefox / navigateur n'est pas sollicité.
   Le bridge Android recevra une chaîne du type :
   "18" ou "18,25,18".
*/

function toggleWifiPassword(){
  const input = $('wifiPassword');
  const button = document.querySelector('.wifi-password-toggle');
  if(!input || !button) return;
  const visible = input.type === 'text';
  input.type = visible ? 'password' : 'text';
  button.textContent = visible ? 'Voir' : 'Cacher';
}

function tactile(pattern){
  if(window.Android && typeof window.Android.vibrate==='function'){
    try{
      const values=Array.isArray(pattern) ? pattern : [pattern];
      window.Android.vibrate(values.join(','));
    }catch(e){
      console.warn('Vibration Android indisponible',e);
    }
  }
}



function formatBytes(bytes){
  if(!Number.isFinite(bytes)||bytes<0) return '—';
  if(bytes<1000) return bytes+' o';
  if(bytes<1000000) return (bytes/1000).toFixed(1)+' ko';
  return (bytes/1000000).toFixed(2)+' Mo';
}


function setText(id,value){
  const e=$(id); if(e) e.textContent=value;
}

function networkLabel(d){
  return d.network || 'Aucun';
}

function networkIconLabel(d){
  const n=networkLabel(d).toUpperCase();
  if(n.includes('WIFI')||n.includes('WI-FI')) return 'WiFi';
  if(n.includes('4G')||n.includes('CELL')) return '4G';
  return n;
}

/* =========================================================
   MAP
   ========================================================= */

function initTrackerMap(){
  if(trackerMap || typeof L==='undefined') return;

  trackerMap=L.map('map',{
    zoomControl:false,
    minZoom:12,
    maxZoom:14
  }).setView([46.6,1.89],12);

  L.control.zoom({position:'bottomright'}).addTo(trackerMap);

  L.tileLayer('/maps/{z}/{x}/{y}.png',{
    minZoom:12,
    maxZoom:14,
    maxNativeZoom:14,
    tileSize:256,
    attribution:'&copy; OpenStreetMap'
  }).addTo(trackerMap);

  trackerMap.on('dragstart',()=>{
    mapFollow=false;
    updateFollowButton();
  });

  setTimeout(()=>{
    trackerMap.invalidateSize();
    if(lastTrackerPosition){
      updateMapPosition(
        lastTrackerPosition[0],
        lastTrackerPosition[1]
      );
    }
  },100);
}

function updateMapPosition(lat,lon){
  if(!Number.isFinite(lat)||!Number.isFinite(lon)) return;
  initTrackerMap();
  if(!trackerMap) return;

  const changed=!lastTrackerPosition||lastTrackerPosition[0]!==lat||lastTrackerPosition[1]!==lon;
  lastTrackerPosition=[lat,lon];

  const customIcon=L.divIcon({className:'trak-marker',iconSize:[18,18],iconAnchor:[9,9]});

  if(!trackerMarker){
    trackerMarker=L.marker([lat,lon],{icon:customIcon}).addTo(trackerMap);
    trackerMap.setView([lat,lon],15);
  }else{
    trackerMarker.setLatLng([lat,lon]);
    if(mapFollow&&changed){
      trackerMap.setView([lat,lon],trackerMap.getZoom(),{animate:true});
    }
  }
}


function toggleFollow(){
  mapFollow=!mapFollow;
  updateFollowButton();
  if(mapFollow&&lastTrackerPosition&&trackerMap){
    trackerMap.setView(lastTrackerPosition,Math.max(trackerMap.getZoom(),15),{animate:true});
  }
}

function updateFollowButton(){
  const btn=$('followBtn');
  if(btn) btn.classList.toggle('active',mapFollow);
}

function toggleFullMap(){
  const full=document.body.classList.toggle('full-map');
  $('fullMapBtn').classList.toggle('active',full);
  setTimeout(()=>{if(trackerMap) trackerMap.invalidateSize();},100);
}

/* =========================================================
   NAVIGATION
   ========================================================= */

function showView(name,button){
  document.querySelectorAll('.view').forEach(v=>v.classList.remove('active'));
  if(name!=='home'){
    const view=$('view-'+name);
    if(view) view.classList.add('active');
  }
  document.querySelectorAll('.nav-btn').forEach(b=>b.classList.remove('active'));
  if(button) button.classList.add('active');
  document.body.classList.remove('full-map');
  $('fullMapBtn').classList.remove('active');
  if(name==='home') setTimeout(()=>{initTrackerMap();if(trackerMap)trackerMap.invalidateSize();},80);
}

/* =========================================================
   REAL DASHBOARD DATA
   ========================================================= */


function updateHome(d){
  document.body.classList.remove('state-gps-ok','state-gps-search','state-offline');
  document.body.classList.add(d.hasFix ? 'state-gps-ok' : 'state-gps-search');
  setText('topStatus',d.hasFix?'GPS ok':'Acquisition GPS...');
  setText('lat',Number.isFinite(d.latitude)?d.latitude.toFixed(6)+'° N':'—');
  setText('lon',Number.isFinite(d.longitude)?d.longitude.toFixed(6)+'° E':'—');
  setText('alt',`Alt: ${Number.isFinite(d.altitude) ? d.altitude.toFixed(0) + ' m' : '—'}`);
  setText('networkIcon',networkIconLabel(d));
  setText('signalTop',Number.isFinite(d.signalPercent)?Math.round(d.signalPercent)+'%':'—');
  updateMapPosition(d.latitude,d.longitude);
}

function updateStats(d){
  setText('statFix',d.hasFix?'OK':'Recherche');
  setText('statSats',d.satellites);
  setText('statGpsGlo',(d.gpsSatellites??0)+' / '+(d.glonassSatellites??0));
  setText('statBdsGal',(d.beidouSatellites??0)+' / '+(d.galileoSatellites??0));
  setText('statNetwork',networkLabel(d));
  setText('statSignal',Number.isFinite(d.signalPercent)?Math.round(d.signalPercent)+' %':'—');
  setText('statInternet',d.internetAvailable?'OK':'OFF');
  setText('statTx',Number.isFinite(d.txCount)?d.txCount:'—');
  setText('statMotion',d.motionMode==='IMMOBILE_CONFIRMATION'?'Verrouillage de la position…':(d.motionMode||'—'));
  setText('statMotionReturn',Number(d.motionStationaryConfirmationRemainingMs||0)>0 ? Math.ceil(Number(d.motionStationaryConfirmationRemainingMs)/1000)+' s' : (d.stationaryConfirmed?'Confirmé':'—'));
  setText('statWifi',formatBytes(d.wifiBytes));
  setText('stat4G',formatBytes(d.cellularBytes));
  setText('statTotal',formatBytes(d.totalBytes));
  const pct=d.dataPlanMb>0?(d.estimated4GBytes/(d.dataPlanMb*1000000))*100:0;
  setText('statPlan',pct.toFixed(1)+' %');
  setText('firmwareVersion',d.firmwareVersion||'—');
  setText('serialNumber',d.serialNumber||'—');
}

async function api(url,options={}){
  const r=await fetch(url,{cache:'no-store',...options});
  if(!r.ok) throw new Error(await r.text()||('HTTP '+r.status));
  return r.json();
}

async function refresh(){
  if(refreshBusy) return;
  refreshBusy=true;
  try{
    const previousData=lastData;
    const d = DEV_MODE ? structuredClone(DEV_DATA) : await api('/api/data');

    // /api/data ne contient pas forcément les données de configuration
    // Sentinel. On conserve donc les valeurs déjà chargées par /api/sentinel.
    if(typeof d.trakPhoneConfigured!=='boolean' &&
       previousData && typeof previousData.trakPhoneConfigured==='boolean'){
      d.trakPhoneConfigured=previousData.trakPhoneConfigured;
      d.trakPhone=previousData.trakPhone||'';
    }
    if(typeof d.phoneConfigured!=='boolean' &&
       previousData && typeof previousData.phoneConfigured==='boolean'){
      d.phoneConfigured=previousData.phoneConfigured;
    }

    lastData=d;
    updateHome(d);
    updateStats(d);
    updateSettingsFromData(d);

    // /api/data contient déjà l'état Sentinel et son compte à rebours.
    // Pendant une action utilisateur, on ne laisse pas un refresh concurrent
    // réécraser l'état visuel optimiste (notamment DISARMING).
    if(!sentinelActionInFlight){
      syncSentinelUi(d.sentinelState||'OFF', Number(d.sentinelConfirmationRemainingMs||0));
    }
    updateSentinel(d);
  }catch(e){
    document.body.classList.remove('state-gps-ok','state-gps-search');
    document.body.classList.add('state-offline');
    setText('topStatus','TRAK · OFFLINE');
  }finally{
    refreshBusy=false;
  }
}

/* =========================================================
   SENTINEL
   ========================================================= */

let sentinelPhoneLoaded=false;
let sentinelRefreshBusy=false;
let sentinelActionInFlight=false;

function syncSentinelUi(state, remainingMs){
  const cleanState=state||'OFF';
  const ms=Math.max(0, Number(remainingMs)||0);
  sentinelUiState=cleanState;
  sentinelUiDeadline=(cleanState==='ARMING') ? Date.now()+ms : 0;
  sentinelUiOptimistic=false;
  if(lastData){
    lastData.sentinelState=cleanState;
    lastData.sentinelConfirmationRemainingMs=ms;
  }
  updateSentinel(lastData||{sentinelState:cleanState,sentinelConfirmationRemainingMs:ms});
}

function setSentinelUiOptimistic(state, remainingMs=20000){
  sentinelUiState=state;
  sentinelUiDeadline=Date.now()+remainingMs;
  sentinelUiOptimistic=true;
  if(lastData){
    lastData.sentinelState=state;
    lastData.sentinelConfirmationRemainingMs=remainingMs;
  }
  updateSentinel(lastData||{sentinelState:state,sentinelConfirmationRemainingMs:remainingMs});
}

function startSentinelUiTimer(){
  if(sentinelUiTimer) return;
  sentinelUiTimer=setInterval(()=>{
    if(!sentinelUiState) return;
    if(sentinelUiState!=='ARMING') {
      clearInterval(sentinelUiTimer);
      sentinelUiTimer=null;
      return;
    }
    const remaining=Math.max(0,sentinelUiDeadline-Date.now());
    if(lastData) lastData.sentinelConfirmationRemainingMs=remaining;
    updateSentinel(lastData||{sentinelState:sentinelUiState,sentinelConfirmationRemainingMs:remaining});
    if(remaining<=0 && !sentinelUiOptimistic){
      sentinelUiState=null;
      sentinelUiDeadline=0;
    }
  },250);
}

function updateSentinel(d){
  const serverState=d.sentinelState||'OFF';
  const state=(sentinelUiState && (sentinelUiState==='ARMING'||sentinelUiState==='DISARMING'))
    ? sentinelUiState : serverState;
  const phoneConfigured=(typeof d.phoneConfigured==='boolean') ? d.phoneConfigured : !!d.sentinelPhoneConfigured;
  const remainingMs=(state==='ARMING')
    ? Math.max(0, sentinelUiDeadline ? sentinelUiDeadline-Date.now() : Number(d.sentinelConfirmationRemainingMs||0))
    : 0;
  const remaining=Math.ceil(remainingMs/1000);
  const motionRemaining=Math.ceil(Number(d.motionStationaryConfirmationRemainingMs||0)/1000);
  const motionReturn=$('motionReturnCountdown');
  const motionReturnSeconds=$('motionReturnSeconds');
  if(motionReturn && motionReturnSeconds){
    motionReturn.style.display=motionRemaining>0 ? 'block' : 'none';
    motionReturnSeconds.textContent=motionRemaining+' s';
  }
  const stateEl=$('sentinelStateHome');
  const btn=$('sentinelButton');
  const desc=$('sentinelDescription');
  const phoneStatus=$('sentinelPhoneStatus');

  if(stateEl){
    stateEl.textContent=state;
    stateEl.className='sentinel-state '+state.toLowerCase();
  }

  if(phoneStatus){
    phoneStatus.textContent=phoneConfigured
      ? 'Numero utilisateur configure'
      : 'Numero utilisateur non configure';
  }

  const trakPhoneConfigured =
    typeof d.trakPhoneConfigured==='boolean'
      ? d.trakPhoneConfigured
      : (lastData && typeof lastData.trakPhoneConfigured==='boolean'
          ? lastData.trakPhoneConfigured
          : false);
  const trakPhoneInput=$('sentinelTrakPhone');
  const deleteTrakPhoneBtn=$('deleteSentinelTrakPhoneButton');
  const trakPhoneWarning=$('sentinelTrakPhoneWarning');

  if(trakPhoneInput && trakPhoneConfigured && document.activeElement!==trakPhoneInput && d.trakPhone){
    trakPhoneInput.value=d.trakPhone;
  }
  if(trakPhoneWarning){
    trakPhoneWarning.style.display=trakPhoneConfigured ? 'none' : 'block';
  }
  if(deleteTrakPhoneBtn){
    deleteTrakPhoneBtn.disabled=!trakPhoneConfigured || state!=='OFF';
    deleteTrakPhoneBtn.title=state==='OFF' ? 'Supprimer le numero du TRAK' : 'Sentinel doit etre OFF pour supprimer le numero';
  }

  const phoneInput=$('sentinelPhone');
  const deletePhoneBtn=$('deleteSentinelPhoneButton');
  if(phoneInput && phoneConfigured && document.activeElement!==phoneInput && d.phone){
    phoneInput.value=d.phone;
  }
  if(deletePhoneBtn){
    deletePhoneBtn.disabled=!phoneConfigured || state!=='OFF';
    deletePhoneBtn.title=state==='OFF' ? 'Supprimer le numero utilisateur' : 'Sentinel doit etre OFF pour supprimer le numero';
  }

  if(!phoneConfigured && d.communicationReady &&
     !sessionStorage.getItem('trakSentinelPhonePromptShown')){
    sessionStorage.setItem('trakSentinelPhonePromptShown','1');
    setTimeout(()=>{
      const phone=window.prompt('Premiere mise en service Sentinel\\n\\nEntrez le numero de telephone de l’utilisateur qui recevra les SMS :');
      if(phone && phone.trim()){
        if($('sentinelPhone')) $('sentinelPhone').value=phone.trim();
        saveSentinelPhone();
      }else{
        showView('settings',document.querySelector('[data-view="settings"]'));
      }
    },500);
  }

  if(!btn||!desc) return;

  const actionBusy=btn.classList.contains('busy');
  btn.className='sentinel-btn'+(actionBusy?' busy':'');
  btn.disabled=actionBusy;
  btn.title='';

  if(!phoneConfigured){
    btn.textContent='Configurer le numero';
    btn.classList.add('confirm');
    desc.textContent='Un numero utilisateur est necessaire pour utiliser Sentinel.';
    return;
  }

  if(state==='OFF'){
    const motionMode=d.motionMode||'';
    const locking=(motionMode==='IMMOBILE_CONFIRMATION');
    if(locking){
      btn.textContent='Activation indisponible';
      btn.disabled=true;
      btn.title='Attendez la confirmation d’immobilisation avant d’activer Sentinel.';
      desc.textContent='Verrouillage de la position en cours… Attendez la confirmation d’immobilisation avant d’activer Sentinel.';
    }else{
      btn.innerHTML='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 640"><path d="M320 64C302.3 64 288 78.3 288 96L288 99.2C215 114 160 178.6 160 256L160 277.7C160 325.8 143.6 372.5 113.6 410.1L103.8 422.3C98.7 428.6 96 436.4 96 444.5C96 464.1 111.9 480 131.5 480L508.4 480C528 480 543.9 464.1 543.9 444.5C543.9 436.4 541.2 428.6 536.1 422.3L526.3 410.1C496.4 372.5 480 325.8 480 277.7L480 256C480 178.6 425 114 352 99.2L352 96C352 78.3 337.7 64 320 64zM258 528C265.1 555.6 290.2 576 320 576C349.8 576 374.9 555.6 382 528L258 528z"/></svg> Activer Sentinel';
      desc.textContent='Appuyez une fois pour activer Sentinel, vous devrez confirmer sous 20s l\'activation.';
    }
  }else if(state==='ARMING'){
    btn.textContent='Confirmer activation · '+remaining+' s';
    btn.classList.add('confirm');
    desc.textContent='SMS de validation envoyé. Appuyez à nouveau sur le même bouton dans les 20 s pour confirmer.';
  }else if(state==='ON'){
    btn.innerHTML='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 640"><path d="M73 39.1C63.6 29.7 48.4 29.7 39.1 39.1C29.8 48.5 29.7 63.7 39 73.1L567 601.1C576.4 610.5 591.6 610.5 600.9 601.1C610.2 591.7 610.3 576.5 600.9 567.2L513.4 479.7C530.6 477.3 543.9 462.4 543.9 444.5C543.9 436.4 541.2 428.6 536.1 422.3L526.3 410.1C496.4 372.5 480 325.8 480 277.7L480 256C480 178.6 425 114 352 99.2L352 96C352 78.3 337.7 64 320 64C302.3 64 288 78.3 288 96L288 99.2C249.4 107 215.8 128.8 192.8 158.9L73 39.1zM160 277.6C160 325.7 143.6 372.4 113.6 410L103.8 422.2C98.8 428.5 96 436.3 96 444.4C96 464 111.9 479.9 131.5 479.9L366.8 479.9L159.9 273L159.9 277.5zM320 576C349.8 576 374.9 555.6 382 528L258 528C265.1 555.6 290.2 576 320 576z"/></svg> Desactiver Sentinel';
    btn.classList.add('on');
    desc.textContent='Surveillance LSM6DS3 active · alerte SMS au passage immobile → mobile.';
  }else if(state==='DISARMING'){
    btn.textContent='Confirmer desactivation';
    btn.classList.add('confirm');
    desc.textContent='SMS de validation envoyé. Appuyez à nouveau pour confirmer la desactivation. Aucun délai.';
  }
}

async function loadSentinel(){
  try{
    const d=await api('/api/sentinel');
    if($('sentinelPhone') && document.activeElement!==$('sentinelPhone')){
      $('sentinelPhone').value=d.phone||'';
    }
    if($('sentinelTrakPhone') && document.activeElement!==$('sentinelTrakPhone')){
      $('sentinelTrakPhone').value=d.trakPhone||'';
    }
    if(lastData){
      lastData.phoneConfigured=!!d.phoneConfigured;
      lastData.sentinelPhoneConfigured=!!d.phoneConfigured;
      lastData.trakPhoneConfigured=!!d.trakPhoneConfigured;
      lastData.trakPhone=d.trakPhone||'';
      if(!sentinelUiOptimistic || (d.state!=='ARMING' && d.state!=='DISARMING')){
        syncSentinelUi(d.state||lastData.sentinelState||'OFF', Number(d.confirmationRemainingMs||0));
      }
    }
    sentinelPhoneLoaded=true;

  }catch(e){
    sentinelPhoneLoaded=false;
  }
}

async function saveSentinelPhone(){
  const input=$('sentinelPhone');
  const phone=input?.value.trim()||'';
  if(!phone){
    alert('Indiquez le numero de telephone de l’utilisateur.');
    return;
  }

  try{
    const r=await fetch('/api/sentinel/phone',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({phone})
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    if($('sentinelPhoneHint')) $('sentinelPhoneHint').textContent='Numero enregistre. SMS de confirmation envoye.';
    input.style.borderColor='var(--success)';
    setTimeout(()=>input.style.borderColor='',1200);
    await loadSentinel();
    await refresh();
  }catch(e){
    input.style.borderColor='var(--danger)';
    alert(e.message);
  }
}

async function saveSentinelTrakPhone(){
  const input=$('sentinelTrakPhone');
  const phone=input?.value.trim()||'';
  if(!phone){
    alert('Indiquez le numero de telephone du TRAK.');
    return;
  }

  try{
    const r=await fetch('/api/sentinel/trak-phone',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({phone})
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    input.style.borderColor='var(--success)';
    setTimeout(()=>input.style.borderColor='',1200);
    await loadSentinel();
    await refresh();
  }catch(e){
    input.style.borderColor='var(--danger)';
    alert(e.message);
  }
}

async function deleteSentinelTrakPhone(){
  const input=$('sentinelTrakPhone');
  if(!input?.value.trim()){
    input && (input.value='');
    return;
  }
  if(lastData?.sentinelState && lastData.sentinelState!=='OFF'){
    alert('Sentinel doit etre OFF pour supprimer le numero.');
    return;
  }
  if(!confirm('Supprimer le numero de telephone du TRAK ?\n\nLe TRAK sera signale comme non configure tant qu’un nouveau numero ne sera pas enregistre.')) return;

  try{
    const r=await fetch('/api/sentinel/trak-phone/delete',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:'{}'
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    input.value='';
    await loadSentinel();
    await refresh();
  }catch(e){
    alert(e.message);
  }
}


async function deleteSentinelPhone(){
  const input=$('sentinelPhone');
  if(!input?.value.trim()){
    input && (input.value='');
    return;
  }
  if(lastData?.sentinelState && lastData.sentinelState!=='OFF'){
    alert('Sentinel doit etre OFF pour supprimer le numero.');
    return;
  }
  if(!confirm('Supprimer le numero de telephone utilisateur du TRAK ?\n\nLes SMS Sentinel ne pourront plus etre envoyes tant qu’un nouveau numero ne sera pas enregistre.')) return;

  try{
    const r=await fetch('/api/sentinel/phone/delete',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:'{}'
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    input.value='';
    if($('sentinelPhoneHint')) $('sentinelPhoneHint').textContent='Numero utilisateur supprime du TRAK.';
    await loadSentinel();
    await refresh();
  }catch(e){
    alert(e.message);
  }
}

async function advanceSentinel(event){
  event?.preventDefault?.();
  event?.stopPropagation?.();
  const state=lastData?.sentinelState||'OFF';
  const btn=$('sentinelButton');

  if(!(lastData?.sentinelPhoneConfigured ?? lastData?.phoneConfigured)){
    showView('settings',document.querySelector('[data-view="settings"]'));
    $('sentinelPhone')?.focus();
    alert('Configurez d’abord le numero de telephone de l’utilisateur.');
    return;
  }

  if(state!=='OFF' && state!=='ARMING' && state!=='ON' && state!=='DISARMING') return;

  // Retour tactile et état visuel immédiats : le navigateur n'attend plus le SMS.
  sentinelActionInFlight=true;
  tactile(state==='ON'?[18,25,18]:18);
  if(btn){
    btn.classList.add('busy');
    btn.disabled=true;
  }

  const optimisticState = state==='OFF' ? 'ARMING' :
                          state==='ARMING' ? 'ON' :
                          state==='ON' ? 'DISARMING' : 'OFF';

  // Seule l'activation (ARMING) utilise un compte a rebours.
  // Le prochain /api/data resynchronisera avec le temps réellement restant côté ESP32.
  if(optimisticState==='ARMING'){
    setSentinelUiOptimistic(optimisticState,20000);
    startSentinelUiTimer();
  }else{
    syncSentinelUi(optimisticState,0);
  }

  try{
    const r=await fetch('/api/sentinel/action',{
      method:'POST',
      cache:'no-store',
      headers:{'Content-Type':'application/json'},
      body:'{}'
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));

    tactile(state==='ON'?[18,35,18]:[18,30,18]);

    // L'API peut retourner l'état réel immédiatement après l'action.
    try{
      const result=JSON.parse(text);
      if(result.state){
        syncSentinelUi(result.state,Number(result.confirmationRemainingMs||0));
      }
    }catch(_){ }

    // Une seule synchronisation complète : /api/data contient déjà Sentinel.
    await refresh();
  }catch(e){
    tactile([45,30,45]);
    // En cas d'échec SMS/API, on abandonne l'état optimiste et on relit l'état réel.
    sentinelUiOptimistic=false;
    sentinelUiState=null;
    sentinelUiDeadline=0;
    await refresh();
    alert(e.message);
  }finally{
    sentinelActionInFlight=false;
    if(btn){
      btn.classList.remove('busy');
      btn.disabled=false;
    }
  }
}

const motionSensitivityNames={1:'TRÈS SENSIBLE',2:'SENSIBLE',3:'MOYEN',4:'PEU SENSIBLE',5:'TRÈS PEU SENSIBLE'};

function updateMotionSensitivityUI(level,threshold){
  const l=Math.min(5,Math.max(1,Number(level)||3));
  const slider=$('motionSensitivitySlider');
  const value=$('motionSensitivityValue');
  const hint=$('motionSensitivityHint');
  if(slider && document.activeElement!==slider) slider.value=l;
  if(value) value.textContent=motionSensitivityNames[l];
  if(hint) hint.textContent='Seuil actuel : '+Number(threshold??6).toFixed(1)+' deg/s · détection par norme du gyroscope.';
}

function previewMotionSensitivity(level){
  const thresholds={1:2,2:4,3:6,4:10,5:15};
  updateMotionSensitivityUI(level,thresholds[level]??thresholds[3]);
}

let motionSensitivitySaveBusy=false;
async function saveMotionSensitivity(level){
  if(motionSensitivitySaveBusy) return;
  const l=Math.min(5,Math.max(1,Number(level)||3));
  motionSensitivitySaveBusy=true;
  const slider=$('motionSensitivitySlider');
  if(slider) slider.disabled=true;
  try{
    const r=await fetch('/api/motion-sensitivity',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({level:l})});
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    const d=JSON.parse(text);
    updateMotionSensitivityUI(d.level,d.thresholdDps);
    tactile(18);
  }catch(e){
    alert(e.message);
    await refresh();
  }finally{
    motionSensitivitySaveBusy=false;
    if(slider) slider.disabled=false;
  }
}

/* =========================================================
   SETTINGS — REAL API
   ========================================================= */

const recordIntervalValues={1:5,2:10,3:15,4:20,5:25};

function updateRecordIntervalUI(level,seconds){
  const l=Math.min(5,Math.max(1,Number(level)||3));
  const sec=Number(seconds)||recordIntervalValues[l]||15;
  const slider=$("recordIntervalSlider");
  const value=$("recordIntervalValue");
  const hint=$("recordIntervalHint");
  if(slider && document.activeElement!==slider) slider.value=l;
  if(value) value.textContent=sec+'s';
  if(hint) hint.textContent='Intervalle actuel : '+sec+'s';
}

function previewRecordInterval(level){
  const l=Math.min(5,Math.max(1,Number(level)||3));
  updateRecordIntervalUI(l,recordIntervalValues[l]);
}

let recordIntervalSaveBusy=false;
async function saveRecordInterval(level){
  if(recordIntervalSaveBusy) return;
  const l=Math.min(5,Math.max(1,Number(level)||3));
  recordIntervalSaveBusy=true;
  const slider=$("recordIntervalSlider");
  if(slider) slider.disabled=true;
  try{
    const r=await fetch('/api/send-interval',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({level:l})});
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    const d=JSON.parse(text);
    updateRecordIntervalUI(d.level,d.seconds);
    tactile(18);
  }catch(e){
    alert(e.message);
    await refresh();
  }finally{
    recordIntervalSaveBusy=false;
    if(slider) slider.disabled=false;
  }
}

function updateSettingsFromData(d){
  const active=String(d.network||'').trim();
  setText('networkMode',active||'Offline');
  setText('signalSetting',Number.isFinite(d.signalPercent)?Math.round(d.signalPercent)+' %':'—');
  if ($('dataPlanMb') && document.activeElement !== $('dataPlanMb')) {
    $('dataPlanMb').value = Number.isFinite(Number(d.dataPlanMb)) ? d.dataPlanMb : '';
  }
  setText('data4GSetting',formatBytes(d.cellularBytes));
  setText('dataEstimatedSetting',formatBytes(d.estimated4GBytes));
  updateMotionSensitivityUI(d.motionSensitivityLevel||3,d.motionSensitivityThresholdDps||6);
  updateRecordIntervalUI(d.sendIntervalLevel||3,d.sendIntervalMovingSec||15);
}

let wifiProfilesCache=[];

async function loadProfiles(){
  try{
    const profiles=await api('/api/wifi');
    wifiProfilesCache=Array.isArray(profiles)?profiles:[];
    renderWifiProfiles();
  }catch(e){
    wifiProfilesCache=[];
    renderWifiProfiles(true);
  }
}

function getWifiSlot(slot){
  return wifiProfilesCache.find(p=>Number(p.slot)===Number(slot)) ||
    {slot:Number(slot),configured:false,ssid:''};
}

function escapeHtml(value){
  return String(value??'').replace(/[&<>"']/g,m=>({
    '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#039;'
  }[m]));
}

function renderWifiProfiles(error=false){
  const slots=[0,1,2].map(getWifiSlot);
  const configured=slots.filter(p=>p.configured && p.ssid);

  const preview=$('wifiProfilesPreview');
  if(preview){
    preview.innerHTML=slots.map(p=>{
      const label='WF'+(Number(p.slot)+1);
      if(p.configured && p.ssid){
        return `<div class="wifi-profile">
          <div class="wifi-profile-info">
            <div class="wifi-profile-slot">${label}</div>
            <div class="wifi-profile-ssid">${escapeHtml(p.ssid)}</div>
          </div>
          <div class="wifi-profile-actions">
            <button class="wifi-mini-btn" onclick="openWifiModal(${Number(p.slot)})">Modifier</button>
            <button class="wifi-mini-btn danger" onclick="deleteWifiProfile(${Number(p.slot)})">Suppr.</button>
          </div>
        </div>`;
      }
      return `<div class="wifi-profile">
        <div class="wifi-profile-info">
          <div class="wifi-profile-slot">${label}</div>
          <div class="wifi-profile-ssid">Vide</div>
        </div>
        <div class="wifi-profile-actions">
          <button class="wifi-mini-btn" onclick="openWifiModal(${Number(p.slot)})">Ajouter</button>
        </div>
      </div>`;
    }).join('');
  }

  setText('wifiSummary',
    error ? 'Impossible de lire les profils Wi-Fi' :
    configured.length ? `${configured.length}/3 réseau${configured.length>1?'x':''} enregistré${configured.length>1?'s':''}` :
    'Aucun réseau enregistré · 4G automatique'
  );
  renderWifiModalSlots();

  if(!error && configured.length===0 && !sessionStorage.getItem('trakWifiNoProfilePopupShown')){
    sessionStorage.setItem('trakWifiNoProfilePopupShown','1');
    setTimeout(openWifiNoProfilePopup,250);
  }
}

function renderWifiModalSlots(){
  const box=$('wifiSlots');
  if(!box) return;
  box.innerHTML=[0,1,2].map(slot=>{
    const p=getWifiSlot(slot);
    const configured=!!(p.configured && p.ssid);
    return `<div class="wifi-slot">
      <div class="wifi-slot-main">
        <div class="wifi-slot-label">WF${slot+1}</div>
        <div class="wifi-slot-ssid">${configured?escapeHtml(p.ssid):'Aucun réseau enregistré'}</div>
      </div>
      <div class="wifi-slot-status ${configured?'ok':'empty'}">${configured?'ENREGISTRÉ':'VIDE'}</div>
    </div>`;
  }).join('');
}

function openWifiModal(slot=null){
  renderWifiModalSlots();
  $('wifiModal').classList.add('active');

  if(slot===null){
    const empty=wifiProfilesCache.find(p=>!p.configured);
    slot=empty ? Number(empty.slot) : null;
  }
  if(slot!==null) editWifiProfile(Number(slot));
  else clearWifiForm();
  setTimeout(()=>$('wifiSsid')?.focus(),50);
}

function closeWifiModal(){
  $('wifiModal').classList.remove('active');
  clearWifiForm();
}

function wifiModalBackdrop(event){
  if(event.target===$('wifiModal')) closeWifiModal();
}

function openWifiNoProfilePopup(){
  $('wifiFormHint').textContent='Aucun réseau Wi-Fi enregistré. Enregistrer un réseau local pour permettre au TRAK de l’utiliser. « Plus tard » laisse le TRAK en 4G.';
  openWifiModal();
  const actions=document.querySelector('.wifi-form-actions');
  if(actions && !$('wifiLaterBtn')){
    const b=document.createElement('button');
    b.id='wifiLaterBtn';
    b.className='btn-secondary';
    b.textContent='Plus tard';
    b.onclick=closeWifiModal;
    actions.appendChild(b);
  }
}

function editWifiProfile(slot){
  const p=getWifiSlot(slot);
  $('wifiSlot').value=slot;
  $('wifiSsid').value=p.configured?p.ssid:'';
  $('wifiPassword').value='';
  $('wifiFormTitle').textContent=p.configured?`Modifier WF${slot+1}`:`Enregistrer WF${slot+1}`;
  $('wifiFormHint').textContent=p.configured
    ? 'Le mot de passe actuel n’est jamais renvoyé par le TRAK. Saisissez-en un nouveau pour le remplacer.'
    : 'Le mot de passe sera stocké dans le TRAK et ne sera jamais renvoyé au dashboard.';
}

function clearWifiForm(){
  if(!$('wifiSlot')) return;
  $('wifiSlot').value='';
  $('wifiSsid').value='';
  $('wifiPassword').value='';
  $('wifiFormTitle').textContent='Enregistrer un réseau';
  $('wifiFormHint').textContent='Si aucun réseau Wi-Fi n’est enregistré, le TRAK passe automatiquement en 4G.';
}

async function saveWifiProfile(){
  const slot=Number($('wifiSlot').value);
  const ssid=$('wifiSsid').value.trim();
  const password=$('wifiPassword').value;

  if(!Number.isInteger(slot)||slot<0||slot>2){alert('Emplacement Wi-Fi invalide.');return;}
  if(!ssid){alert('Indiquez le nom du réseau Wi-Fi (SSID).');return;}
  if(!password){alert('Indiquez le mot de passe Wi-Fi.');return;}

  try{
    const r=await fetch('/api/wifi',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({slot,ssid,password})
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    await loadProfiles();
    await refresh();
    closeWifiModal();
  }catch(e){alert(e.message);}
}

async function deleteWifiProfile(slot){
  const p=getWifiSlot(slot);
  if(!p.configured) return;
  if(!confirm(`Supprimer le réseau « ${p.ssid} » de WF${slot+1} ?`)) return;

  try{
    const r=await fetch('/api/wifi?slot='+encodeURIComponent(slot),{method:'DELETE'});
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));
    await loadProfiles();
    await refresh();
    if(!wifiProfilesCache.some(x=>x.configured&&x.ssid)){
      setTimeout(openWifiNoProfilePopup,250);
    }
  }catch(e){alert(e.message);}
}

async function loadTrackserver(){
  try{
    const d=await api('/api/trackserver');
    $('trackserverUrl').value=d.url||'';
  }catch(e){$('trackserverUrl').value='';}
}

async function saveTrackserver(){
  const input=$('trackserverUrl');
  const old=input.style.borderColor;
  try{
    const r=await fetch('/api/trackserver',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url:input.value.trim()})});
    const text=await r.text();
    if(!r.ok) throw new Error(text);
    input.style.borderColor='var(--success)';
    setTimeout(()=>input.style.borderColor=old,900);
  }catch(e){
    input.style.borderColor='var(--danger)';
    alert(e.message);
  }
}

async function saveDataPlan(){
  const input=$('dataPlanMb');
  const planMb=parseInt(input.value,10);

  if(!Number.isInteger(planMb) || planMb<1 || planMb>1000000){
    input.style.borderColor='var(--danger)';
    alert('Forfait invalide : indiquez une valeur entre 1 et 1 000 000 Mo.');
    return;
  }

  const coefficient=Number(lastData?.dataCoefficient ?? 1.0);

  try{
    const r=await fetch('/api/data-usage',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({planMb,coefficient})
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||('HTTP '+r.status));

    input.style.borderColor='var(--success)';
    setTimeout(()=>input.style.borderColor='',900);
    await refresh();
  }catch(e){
    input.style.borderColor='var(--danger)';
    alert(e.message);
  }
}

/* =========================================================
   START
   ========================================================= */


/* =========================================================
   2027 TOUCH / CLICK INTERACTIONS
   ========================================================= */
function setupInteractionMotion(){
  const interactiveSelector=[
    'button','.map-btn','.nav-btn','.wifi-mini-btn','.wifi-open-btn',
    '.sentinel-btn','.btn-primary','.btn-secondary','.switch'
  ].join(',');

  document.querySelectorAll(interactiveSelector).forEach(el=>el.classList.add('pressable'));

  document.addEventListener('pointerdown',(event)=>{
    const target=event.target.closest(interactiveSelector);
    if(!target || target.disabled) return;
    target.classList.add('is-pressing');

    const rect=target.getBoundingClientRect();
    const ripple=document.createElement('span');
    ripple.className='tap-ripple';
    ripple.style.left=(event.clientX-rect.left)+'px';
    ripple.style.top=(event.clientY-rect.top)+'px';
    target.appendChild(ripple);
    ripple.addEventListener('animationend',()=>ripple.remove(),{once:true});
  },{passive:true});

  const release=event=>{
    const target=event.target.closest(interactiveSelector);
    if(target) target.classList.remove('is-pressing');
  };
  document.addEventListener('pointerup',release,{passive:true});
  document.addEventListener('pointercancel',release,{passive:true});
  document.addEventListener('pointerleave',release,{passive:true});

  // Native Android vibration is used when available; browsers stay silent.
  document.addEventListener('click',(event)=>{
    const target=event.target.closest('button,.map-btn,.nav-btn,.wifi-mini-btn,.wifi-open-btn,.sentinel-btn');
    if(!target || target.disabled) return;
    if(target.matches('.sentinel-btn')) tactile([12,18]);
  },{passive:true});

  // Smooth keyboard feedback for desktop users.
  document.addEventListener('keydown',(event)=>{
    if(event.key!=='Enter' && event.key!==' ') return;
    const target=event.target.closest('button,.map-btn,.nav-btn,.wifi-mini-btn,.wifi-open-btn,.sentinel-btn');
    if(target && !target.disabled) target.classList.add('is-pressing');
  });
  document.addEventListener('keyup',(event)=>{
    if(event.key!=='Enter' && event.key!==' ') return;
    const target=event.target.closest('button,.map-btn,.nav-btn,.wifi-mini-btn,.wifi-open-btn,.sentinel-btn');
    if(target) target.classList.remove('is-pressing');
  });
}

document.addEventListener('DOMContentLoaded',()=>{
  setupInteractionMotion();
  initTrackerMap();
  startSentinelUiTimer();
  refresh();
  // Charge aussi le numéro Sentinel depuis l'API dédiée.
  // /api/data peut ne pas exposer le numéro pour des raisons de sécurité.
  loadSentinel();
  loadProfiles();
  loadTrackserver();
  setInterval(refresh,1000);
  setInterval(loadProfiles,10000);
  window.addEventListener('resize',()=>{if(trackerMap)trackerMap.invalidateSize();});
});