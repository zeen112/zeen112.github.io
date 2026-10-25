import { CMD, RSP, frame, saveFrame, nameFrame, text } from './protocol.js';
import { SerialLink } from './serial.js';
import { PATTERNS, buildMap, encodeLayout } from './layout.js';
import { flashFirmware } from './flash.js';

const $ = id => document.getElementById(id);
const LIMIT = 30000;          // batas byte animasi (sama dengan firmware)
const MAX_CELLS = 4096;       // sama dengan MAX_CANVAS_CELLS
const MAX_STRIP_LEN = 256;    // sama dengan MAX_LEDS_PER_STRIP
const CELL = 20;
const PAL = ['#ff0000', '#ff8c00', '#00ff00', '#ffffff', '#0078ff', '#000000'];
const MODE_LABEL = { preset: 'Preset', live: 'Canvas live', file: 'Animasi tersimpan', welcome: 'Welcome', off: 'Mati' };

const S = {
  W: 16, H: 16, strips: 1, lengths: [256, 0, 0, 0], pattern: 'row_zigzag', reverse: false,
  frames: [], cur: 0, color: [255, 0, 0], tool: 'pen', drawing: false,
  link: null, port: null, dev: {}, presetCount: -1,
  sendT: null, sending: false, dirty: false, brT: null,
  playT: null, pvIdx: 0,
};

const grid = $('grid'), gctx = grid.getContext('2d');
const pv = $('preview'), pctx = pv.getContext('2d');

/* ---------- util ---------- */
const blank = () => new Uint8Array(S.W * S.H * 3);
const maxFrames = () => Math.max(1, Math.floor(LIMIT / (S.W * S.H * 3)));
const fps = () => +$('fps').value;
const hexToRgb = h => [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
function note(t, err = false) { $('msg').textContent = t; $('msg').className = err ? 'err' : ''; }
function flashLog(t) { $('flashLog').textContent += t + '\n'; $('flashLog').scrollTop = 1e9; }

function setConn(ok) {
  $('dot').classList.toggle('ok', ok);
  $('connBtn').textContent = ok ? 'Terputus' : 'Hubungkan';
  if (!ok) $('stxt').textContent = 'Belum terhubung';
}

/* ---------- koneksi & pengiriman ---------- */
async function send(bytes) {
  if (!S.link || !S.link.writer) { note('Belum terhubung ke perangkat', true); return false; }
  try { await S.link.send(bytes); return true; }
  catch (e) { note('Gagal mengirim: ' + e.message, true); return false; }
}

function handleFrame(type, payload) {
  if (type === RSP.STATUS) {
    S.dev = JSON.parse(text(payload));
    updateStatus();
  } else if (type === RSP.LIST) {
    renderList(JSON.parse(text(payload)).items);
  } else if (type === RSP.ANIM) {
    loadAnim(payload);
  } else if (type === RSP.MSG) {
    const s = text(payload);
    note(s.slice(1), s[0] !== 'O');
  }
}

async function connect(useExisting) {
  if (!SerialLink.supported()) { alert('Browser ini tidak mendukung Web Serial. Gunakan Chrome atau Edge di desktop.'); return; }
  try {
    if (S.link && S.link.writer) { await S.link.disconnect(); setConn(false); return; }
    S.link = new SerialLink({
      onFrame: handleFrame,
      onClose: () => { setConn(false); note('Koneksi USB terputus', true); },
    });
    let existing = null;
    if (useExisting) {
      const ports = await navigator.serial.getPorts();
      existing = ports[0] || null;
    }
    S.port = await S.link.connect(existing);
    setConn(true);
    note('Terhubung. Mengirim layout…');
    await send(frame(CMD.HELLO));
    await sendLayout();
    await send(frame(CMD.LIST));
  } catch (e) {
    note('Gagal menghubungkan: ' + e.message, true);
  }
}

/* ---------- layout ---------- */
function layoutValid() {
  if (S.W * S.H > MAX_CELLS) return 'Kanvas maksimal ' + MAX_CELLS + ' dot';
  for (let i = 0; i < S.strips; i++) {
    const l = S.lengths[i];
    if (!(l >= 1 && l <= MAX_STRIP_LEN)) return 'Panjang strip ' + (i + 1) + ' harus 1-' + MAX_STRIP_LEN;
  }
  return null;
}

async function sendLayout() {
  const err = layoutValid();
  if (err) return note(err, true);
  const lens = S.lengths.slice(0, S.strips);
  const map = buildMap(S.W, S.H, lens, S.pattern, S.reverse);
  const payload = encodeLayout(S.W, S.H, lens, map);
  if (await send(frame(CMD.LAYOUT, payload))) note('Layout dikirim ke perangkat');
}

function readLayoutInputs() {
  const w = Math.max(1, Math.min(64, +$('lW').value || 1));
  const h = Math.max(1, Math.min(64, +$('lH').value || 1));
  const changed = w !== S.W || h !== S.H;
  S.W = w; S.H = h;
  S.strips = +$('lStrips').value;
  for (let i = 0; i < 4; i++) {
    S.lengths[i] = Math.max(0, Math.min(MAX_STRIP_LEN, +$('len' + i).value || 0));
    $('len' + i).disabled = i >= S.strips;
    $('wrapLen' + i).style.opacity = i >= S.strips ? 0.4 : 1;
  }
  S.pattern = $('lPattern').value;
  S.reverse = $('lReverse').checked;
  if (changed) { S.frames = [blank()]; S.cur = 0; resizeCanvas(); }
  redraw();
}

function buildLayoutUI() {
  const pat = $('lPattern');
  Object.entries(PATTERNS).forEach(([k, v]) => {
    const o = document.createElement('option'); o.value = k; o.textContent = v; pat.appendChild(o);
  });
  pat.value = S.pattern;
  $('lW').value = S.W; $('lH').value = S.H; $('lStrips').value = S.strips;
  for (let i = 0; i < 4; i++) $('len' + i).value = S.lengths[i];
  const onChange = () => { readLayoutInputs(); };
  ['lW', 'lH', 'lStrips', 'lPattern', 'lReverse', 'len0', 'len1', 'len2', 'len3'].forEach(id => $(id).addEventListener('change', onChange));
  $('applyLayout').onclick = async () => { readLayoutInputs(); await sendLayout(); };
}

/* ---------- format animasi ---------- */
function encodeAnim() {
  const sz = S.W * S.H * 3, out = new Uint8Array(10 + sz * S.frames.length);
  out.set([0x53, 0x4c, 0x41, 0x4e, S.W, S.H, S.frames.length & 255, S.frames.length >> 8, fps(), 0]);
  S.frames.forEach((f, i) => out.set(f, 10 + i * sz));
  return out;
}
function loadAnim(u) {
  if (u.length < 10 || String.fromCharCode(u[0], u[1], u[2], u[3]) !== 'SLAN') return note('Data bukan animasi', true);
  const w = u[4], h = u[5], n = u[6] | (u[7] << 8);
  if (w !== S.W || h !== S.H || n === 0) return note('Ukuran animasi tidak cocok dengan layout', true);
  const sz = S.W * S.H * 3;
  S.frames = [];
  for (let i = 0; i < n; i++) S.frames.push(u.slice(10 + i * sz, 10 + (i + 1) * sz));
  S.cur = 0;
  $('fps').value = u[8];
  redraw();
  note('Animasi dimuat ke canvas untuk diedit');
}

/* ---------- kirim realtime (animasi berjalan terus di LED) ---------- */
function scheduleLive() {
  clearTimeout(S.sendT);
  S.sendT = setTimeout(sendLive, 120);
}
async function sendLive() {
  if (!$('liveChk').checked || !S.link || !S.link.writer) return;
  if (S.sending) { S.dirty = true; return; }
  S.sending = true;
  await send(frame(CMD.LIVE, encodeAnim()));
  S.sending = false;
  if (S.dirty) { S.dirty = false; scheduleLive(); }
}

/* ---------- status & daftar ---------- */
function updateStatus() {
  const d = S.dev;
  if (d.presets && d.presets.length !== S.presetCount) {
    S.presetCount = d.presets.length;
    const s = $('presetSel'); s.innerHTML = '';
    d.presets.forEach((n, i) => { const o = document.createElement('option'); o.value = i; o.textContent = (i + 1) + '. ' + n; s.appendChild(o); });
  }
  if (document.activeElement !== $('presetSel')) $('presetSel').value = d.preset;
  if (document.activeElement !== $('bright')) $('bright').value = d.bright;
  $('bPreset').classList.toggle('on', d.mode === 'preset');
  $('bLive').classList.toggle('on', d.mode === 'live');
  $('bWelcome').classList.toggle('on', d.mode === 'welcome');
  $('bOff').classList.toggle('on', d.mode === 'off');
  $('stxt').textContent = 'Terhubung · mode: ' + (MODE_LABEL[d.mode] || d.mode) + (d.file ? ' (' + d.file + ')' : '') + ' · fw ' + d.fw;
}

function renderList(items) {
  const box = $('fileList'); box.innerHTML = '';
  if (!items.length) { box.textContent = 'Belum ada animasi tersimpan di perangkat.'; return; }
  items.forEach(n => {
    const row = document.createElement('div'); row.className = 'item';
    const s = document.createElement('span'); s.textContent = n;
    const g = document.createElement('div'); g.className = 'row';
    const bE = document.createElement('button'); bE.textContent = 'Edit';
    bE.onclick = () => send(nameFrame(CMD.GET, n));
    const bP = document.createElement('button'); bP.textContent = 'Putar';
    bP.onclick = () => send(nameFrame(CMD.PLAY, n));
    const bD = document.createElement('button'); bD.textContent = 'Hapus';
    bD.onclick = () => { if (confirm('Hapus ' + n + ' dari perangkat?')) send(nameFrame(CMD.DELETE, n)); };
    g.append(bE, bP, bD); row.append(s, g); box.appendChild(row);
  });
}

/* ---------- canvas editor ---------- */
function resizeCanvas() {
  grid.width = S.W * CELL; grid.height = S.H * CELL;
  pv.width = S.W * 24; pv.height = S.H * 24;
}
function drawGrid() {
  const f = S.frames[S.cur];
  gctx.fillStyle = '#000'; gctx.fillRect(0, 0, grid.width, grid.height);
  for (let y = 0; y < S.H; y++) for (let x = 0; x < S.W; x++) {
    const i = (y * S.W + x) * 3;
    gctx.fillStyle = `rgb(${f[i]},${f[i + 1]},${f[i + 2]})`;
    gctx.fillRect(x * CELL + 1, y * CELL + 1, CELL - 2, CELL - 2);
  }
}
function drawPreview(f) {
  const cw = pv.width / S.W, ch = pv.height / S.H, r = Math.min(cw, ch) * 0.42;
  pctx.fillStyle = '#000'; pctx.fillRect(0, 0, pv.width, pv.height);
  for (let y = 0; y < S.H; y++) for (let x = 0; x < S.W; x++) {
    const i = (y * S.W + x) * 3;
    const on = f[i] || f[i + 1] || f[i + 2];
    pctx.beginPath(); pctx.arc((x + .5) * cw, (y + .5) * ch, r, 0, Math.PI * 2);
    pctx.fillStyle = on ? `rgb(${f[i]},${f[i + 1]},${f[i + 2]})` : '#1c1c1c';
    pctx.fill();
  }
}
function updUI() {
  $('fIdx').textContent = (S.cur + 1) + '/' + S.frames.length;
  $('addF').disabled = S.frames.length >= maxFrames();
  $('fpsV').textContent = fps();
  $('frameInfo').textContent = `Frame maks ${maxFrames()} (${S.W}×${S.H}, batas ${LIMIT / 1000} KB)`;
}
function redraw() {
  if (S.cur >= S.frames.length) S.cur = S.frames.length - 1;
  drawGrid();
  if (!S.playT) drawPreview(S.frames[S.cur]);
  updUI();
}

function cellAt(ev) {
  const r = grid.getBoundingClientRect();
  return [Math.floor((ev.clientX - r.left) / r.width * S.W), Math.floor((ev.clientY - r.top) / r.height * S.H)];
}
function paint(ev) {
  const [x, y] = cellAt(ev);
  if (x < 0 || y < 0 || x >= S.W || y >= S.H) return;
  const f = S.frames[S.cur], i = (y * S.W + x) * 3;
  if (S.tool === 'erase') { f[i] = f[i + 1] = f[i + 2] = 0; }
  else { f[i] = S.color[0]; f[i + 1] = S.color[1]; f[i + 2] = S.color[2]; }
  drawGrid(); if (!S.playT) drawPreview(f);
  scheduleLive();
}
grid.addEventListener('pointerdown', e => { S.drawing = true; grid.setPointerCapture(e.pointerId); paint(e); });
grid.addEventListener('pointermove', e => { if (S.drawing) paint(e); });
grid.addEventListener('pointerup', () => { S.drawing = false; });
grid.addEventListener('pointercancel', () => { S.drawing = false; });

function setTool(t) {
  S.tool = t;
  $('toolPen').classList.toggle('on', t === 'pen');
  $('toolErase').classList.toggle('on', t === 'erase');
}
function setColor(rgb) { S.color = rgb; setTool('pen'); }

function startPlay() {
  stopPlay(true);
  S.pvIdx = S.cur;
  S.playT = setInterval(() => { S.pvIdx = (S.pvIdx + 1) % S.frames.length; drawPreview(S.frames[S.pvIdx]); }, 1000 / fps());
  $('bPlay').textContent = '■ Stop preview';
}
function stopPlay(silent) {
  clearInterval(S.playT); S.playT = null;
  $('bPlay').textContent = '▶ Putar preview';
  if (!silent) drawPreview(S.frames[S.cur]);
}

/* ---------- simpan & perintah ---------- */
async function saveAnim() {
  const name = $('name').value.trim();
  if (!/^[A-Za-z0-9_-]{1,24}$/.test(name)) return note('Nama 1-24 karakter: huruf, angka, - atau _', true);
  if (await send(saveFrame(name, encodeAnim()))) {
    note('Menyimpan ' + name + '…');
    setTimeout(() => send(frame(CMD.LIST)), 300);
  }
}

/* ---------- flash firmware dari website ---------- */
async function flashNow() {
  if (!SerialLink.supported()) return alert('Gunakan Chrome atau Edge di desktop.');
  if (!confirm('Flash firmware ke ESP32 sekarang? Semua animasi di perangkat tetap ada, tetapi firmware lama akan diganti.')) return;
  $('flashLog').textContent = '';
  $('flashBar').style.width = '0%';
  let port = S.port;
  try {
    if (S.link) await S.link.disconnect();
    setConn(false);
    if (!port) port = await navigator.serial.requestPort();
    await flashFirmware(port, {
      log: flashLog,
      progress: p => { $('flashBar').style.width = p + '%'; $('flashTxt').textContent = 'Flash ' + p + '%'; },
    });
    $('flashTxt').textContent = 'Flash selesai (100%)';
    flashLog('Selesai. Menyambung ulang…');
    await new Promise(r => setTimeout(r, 1500));
    await connectWithPort(port);
  } catch (e) {
    flashLog('GAGAL: ' + e.message);
    flashLog('Tips: tahan tombol BOOT saat "Connecting…", cek kabel data (bukan hanya charging), dan tutup Serial Monitor.');
    $('flashTxt').textContent = 'Flash gagal';
  }
}

async function connectWithPort(port) {
  S.link = new SerialLink({ onFrame: handleFrame, onClose: () => { setConn(false); note('Koneksi USB terputus', true); } });
  try {
    S.port = await S.link.connect(port);
    setConn(true);
    await send(frame(CMD.HELLO));
    await sendLayout();
    await send(frame(CMD.LIST));
  } catch (e) { note('Gagal menyambung ulang: ' + e.message, true); }
}

/* ---------- init ---------- */
function init() {
  buildLayoutUI();
  readLayoutInputs();
  S.frames = [blank()];
  const sw = $('swatches');
  PAL.forEach(h => {
    const b = document.createElement('button');
    b.className = 'swatch'; b.style.background = h; b.title = h;
    b.onclick = () => { setColor(hexToRgb(h)); document.querySelectorAll('.swatch').forEach(s => s.classList.toggle('sel', s === b)); };
    sw.appendChild(b);
  });
  $('toolPen').onclick = () => setTool('pen');
  $('toolErase').onclick = () => setTool('erase');
  $('pickColor').oninput = e => setColor(hexToRgb(e.target.value));

  $('prevF').onclick = () => { S.cur = (S.cur - 1 + S.frames.length) % S.frames.length; redraw(); };
  $('nextF').onclick = () => { S.cur = (S.cur + 1) % S.frames.length; redraw(); };
  $('addF').onclick = () => { if (S.frames.length >= maxFrames()) return; S.frames.splice(S.cur + 1, 0, blank()); S.cur++; redraw(); scheduleLive(); };
  $('dupF').onclick = () => { if (S.frames.length >= maxFrames()) return; S.frames.splice(S.cur + 1, 0, S.frames[S.cur].slice()); S.cur++; redraw(); scheduleLive(); };
  $('delF').onclick = () => {
    if (S.frames.length <= 1) S.frames[0] = blank();
    else { S.frames.splice(S.cur, 1); S.cur = Math.min(S.cur, S.frames.length - 1); }
    redraw(); scheduleLive();
  };
  $('clrF').onclick = () => { S.frames[S.cur] = blank(); redraw(); scheduleLive(); };
  $('bPlay').onclick = () => S.playT ? stopPlay() : startPlay();
  $('fps').oninput = () => { $('fpsV').textContent = fps(); if (S.playT) startPlay(); scheduleLive(); };
  $('bSave').onclick = saveAnim;

  $('connBtn').onclick = () => connect(false);
  $('bPreset').onclick = () => send(frame(CMD.PRESET, Uint8Array.from([+$('presetSel').value])));
  $('bPlayPreset').onclick = () => send(frame(CMD.PRESET, Uint8Array.from([+$('presetSel').value])));
  $('bLive').onclick = () => { S.dirty = false; sendLive(); };
  $('bWelcome').onclick = () => send(frame(CMD.WELCOME));
  $('bOff').onclick = () => send(frame(CMD.OFF));
  $('bright').oninput = e => {
    clearTimeout(S.brT);
    S.brT = setTimeout(() => send(frame(CMD.BRIGHT, Uint8Array.from([+e.target.value]))), 100);
  };
  $('flashBtn').onclick = flashNow;
  $('refreshList').onclick = () => send(frame(CMD.LIST));
  setConn(false);
  redraw();
  // jika sudah pernah diizinkan, sambung otomatis tanpa pilih port lagi
  if (SerialLink.supported()) navigator.serial.getPorts().then(p => { if (p.length) connect(true); }).catch(() => {});
}
init();
