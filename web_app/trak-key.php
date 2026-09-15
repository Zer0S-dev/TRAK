<?php

declare(strict_types=1);
require_once __DIR__ . '/api/config.php';
require_once __DIR__ . '/api/trak/crypto.php';
requireDashboardAuth();
$csrf = htmlspecialchars(csrfToken(), ENT_QUOTES, 'UTF-8');
?><!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>TRAK — Clé de chiffrement</title>
<style>body{font-family:Arial,sans-serif;max-width:620px;margin:40px auto;padding:20px;background:#f6f9fc;color:#222}main{background:#fff;padding:24px;border-radius:14px}label{display:block;font-weight:600;margin:14px 0 6px}input{width:100%;box-sizing:border-box;padding:12px;border:1px solid #bbb;border-radius:8px;font-family:monospace}button{margin-top:18px;padding:12px 18px;border:0;border-radius:22px;background:#183029;color:#b1d600;font-weight:700;cursor:pointer}.status{margin-top:16px;padding:12px;background:#eef0f2;border-radius:8px;white-space:pre-wrap}.warning{font-size:13px}</style></head>
<body><main>
<h1>Clé de chiffrement TRAK</h1>
<p>Colle ici la clé affichée par le Wizard. Elle sera enregistrée dans SQLite, associée au TRAK ID.</p>
<p class="warning"><b>Cette clé est le seul secret du TRAK.</b> Elle ne doit jamais être envoyée dans le JSON du TRAK.</p>
<form id="keyForm">
<label for="trak_id">TRAK ID</label><input id="trak_id" required maxlength="64" placeholder="TRACK-123456">
<label for="encryption_key">Clé de chiffrement</label><input id="encryption_key" required minlength="64" maxlength="64" pattern="[A-Fa-f0-9]{64}" autocomplete="off" placeholder="64 caractères hexadécimaux">
<button type="submit">Enregistrer la clé</button></form>
<div class="status" id="status">Prêt.</div>
</main><script>
const csrf='<?=$csrf?>';
const form=document.getElementById('keyForm'); const status=document.getElementById('status');
form.addEventListener('submit',async e=>{e.preventDefault();status.textContent='Enregistrement...';const body=new URLSearchParams({trak_id:document.getElementById('trak_id').value.trim(),encryption_key:document.getElementById('encryption_key').value.trim()});try{const r=await fetch('/trak/api/trak/key/',{method:'POST',headers:{'X-CSRF-Token':csrf},body});const d=await r.json();if(!r.ok||!d.ok)throw new Error(d.error||('HTTP '+r.status));status.textContent='Clé enregistrée dans SQLite pour '+d.trak_id+'.';}catch(err){status.textContent='Erreur : '+err.message;}});
</script></body></html>
