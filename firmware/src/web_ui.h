// Halaman web bawaan perangkat: status + update firmware (OTA) dengan progres %.
// Dibuka di http://192.168.4.1 setelah WiFi StopLamp-XXXX tersambung.
#pragma once
#include <pgmspace.h>

const char OTA_PAGE_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StopLamp - Firmware</title>
<style>
:root{--bg:#0f1115;--card:#181b22;--tx:#e8eaf0;--mut:#8a90a0;--bd:#2a2e38;--ok:#34c759;--err:#ff6b6b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font-family:system-ui,sans-serif;padding:16px;max-width:560px;margin-inline:auto}
h1{font-size:20px;margin:0 0 12px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:13px;margin:0 0 10px;color:var(--mut);text-transform:uppercase;letter-spacing:.06em}
button{background:#262b36;color:var(--tx);border:1px solid var(--bd);border-radius:8px;padding:10px 14px;font-size:15px;cursor:pointer}
button:disabled{opacity:.4}
.bar{height:14px;background:#262b36;border-radius:7px;overflow:hidden;margin-top:12px}
.bar>div{height:100%;width:0;background:var(--ok);transition:width .2s}
#txt{font-size:14px;color:var(--mut);margin-top:8px;min-height:20px}
#txt.err{color:var(--err)}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;margin:0;font-size:14px}
dt{color:var(--mut)}
dd{margin:0}
</style>
</head>
<body>
<h1>StopLamp - Perangkat</h1>

<div class="card">
  <h2>Status</h2>
  <dl>
    <dt>Firmware</dt><dd id="fw">-</dd>
    <dt>Mode</dt><dd id="mode">-</dd>
    <dt>Kanvas</dt><dd id="size">-</dd>
    <dt>Strip aktif</dt><dd id="strips">-</dd>
  </dl>
</div>

<div class="card">
  <h2>Update firmware (OTA)</h2>
  <p style="font-size:13px;color:var(--mut);margin-top:0">Login: user <b>admin</b>, password = password WiFi StopLamp. Jika upload gagal di tengah, firmware lama tetap aktif.</p>
  <input type="file" id="fw_file" accept=".bin">
  <div style="margin-top:12px"><button id="bOta">Upload firmware</button></div>
  <div class="bar"><div id="bar"></div></div>
  <div id="txt"></div>
</div>

<script>
const $ = id => document.getElementById(id);
function loadStatus(){
  fetch('/api/status').then(r => r.json()).then(s => {
    $('fw').textContent = s.fw;
    $('mode').textContent = s.mode;
    $('size').textContent = s.w + ' x ' + s.h;
    $('strips').textContent = s.strips;
  }).catch(() => {});
}
$('bOta').onclick = () => {
  const f = $('fw_file').files[0];
  if (!f) return alert('Pilih file firmware (.bin) dulu');
  const fd = new FormData();
  fd.append('update', f, 'firmware.bin');
  const x = new XMLHttpRequest();
  x.open('POST', '/api/ota');
  $('bOta').disabled = true;
  x.upload.onprogress = e => {
    if (!e.lengthComputable) return;
    const p = Math.round(e.loaded / e.total * 100);
    $('bar').style.width = p + '%';
    $('txt').textContent = p < 100 ? 'Mengirim firmware… ' + p + '%' : 'Memverifikasi di perangkat…';
  };
  x.onload = () => {
    $('bOta').disabled = false;
    if (x.status === 200) {
      $('bar').style.width = '100%';
      $('txt').textContent = 'Berhasil (100%). Perangkat restart…';
    } else {
      $('txt').className = 'err';
      $('txt').textContent = 'Gagal (' + x.status + '): ' + x.responseText + ' - firmware lama tetap aktif.';
    }
  };
  x.onerror = () => {
    $('bOta').disabled = false;
    $('txt').className = 'err';
    $('txt').textContent = 'Koneksi putus saat upload. Firmware lama tetap aktif, coba ulang.';
  };
  x.send(fd);
};
loadStatus();
</script>
</body>
</html>
)rawliteral";
