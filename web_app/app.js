/* TRAK 3.0.5 Wi-Fi dashboard bridge */
(function(){
  const core=document.createElement('script');
  core.src='app-core.js';
  core.onload=function(){
    const WIFI_API='api/wifi/';
    let wifiProfiles=[];
    async function loadWifiProfiles(){try{const data=await api(WIFI_API);if(data?.ok===true){wifiProfiles=data.profiles||[];renderWifiSlots();}}catch(error){if(error.message!=='unauthorized')console.warn('[TRAK] Wi-Fi:',error);}}
    function renderWifiSlots(){const box=document.getElementById('wifiSlots');if(!box)return;box.innerHTML='';wifiProfiles.forEach(profile=>{const row=document.createElement('div');row.className='wifi-slot';const configured=profile.configured===true;row.innerHTML='<div><strong>Wi-Fi '+(Number(profile.slot)+1)+'</strong><div>'+(profile.ssid||'Aucun réseau')+' · '+(configured?'Configuré':'Non configuré')+'</div></div><button type="button" class="btn-secondary" onclick="editWifiProfile('+Number(profile.slot)+')">'+(configured?'Modifier':'Configurer')+'</button>';box.appendChild(row);});}
    window.openWifiModal=async function(){document.getElementById('wifiModal')?.classList.add('active','open');window.clearWifiForm();await loadWifiProfiles();};
    window.closeWifiModal=function(){document.getElementById('wifiModal')?.classList.remove('active','open');};
    window.wifiModalBackdrop=function(event){if(event.target===document.getElementById('wifiModal'))window.closeWifiModal();};
    window.toggleWifiPassword=function(){const input=document.getElementById('wifiPassword');if(input)input.type=input.type==='password'?'text':'password';};
    window.editWifiProfile=function(slot){const p=wifiProfiles.find(x=>Number(x.slot)===Number(slot));document.getElementById('wifiSlot').value=String(slot);document.getElementById('wifiSsid').value=p?.ssid||'';document.getElementById('wifiPassword').value='';const title=document.getElementById('wifiFormTitle');if(title)title.textContent='Réseau Wi-Fi '+(Number(slot)+1);const hint=document.getElementById('wifiFormHint');if(hint)hint.textContent=p?.configured?'Laissez le mot de passe vide pour conserver celui déjà enregistré.':'Enregistrez le SSID et le mot de passe du réseau.';};
    window.saveWifiProfile=async function(){if(!window.csrf){alert('Session de sécurité indisponible. Rechargez la page.');return;}const slot=Number(document.getElementById('wifiSlot')?.value),ssid=document.getElementById('wifiSsid')?.value.trim()||'',password=document.getElementById('wifiPassword')?.value||'';if(!Number.isInteger(slot)||slot<0||slot>2||!ssid){alert('SSID obligatoire.');return;}try{const body={slot,ssid};if(password)body.password=password;const data=await api(WIFI_API,{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':window.csrf},body:JSON.stringify(body)});if(data?.ok===true){wifiProfiles=data.profiles||[];renderWifiSlots();window.clearWifiForm();alert('Réseau Wi-Fi enregistré. Le TRAK le récupérera lors de sa prochaine synchronisation.');}}catch(error){if(error.message!=='unauthorized')alert(error.message);}};
    window.clearWifiForm=function(){['wifiSlot','wifiSsid','wifiPassword'].forEach(id=>{const el=document.getElementById(id);if(el)el.value='';});const title=document.getElementById('wifiFormTitle');if(title)title.textContent='Enregistrer un réseau';const hint=document.getElementById('wifiFormHint');if(hint)hint.textContent='Jusqu’à 3 réseaux peuvent être mémorisés dans le TRAK.';};
    loadWifiProfiles();
  };
  document.head.appendChild(core);
})();
