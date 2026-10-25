// StopLamp firmware v2 - ESP32, sampai 4 strip LED (WS2812B/SK6812) di pin berbeda.
// - Kontrol utama lewat USB serial (dari editor di Vercel), protokol berbingkai + CRC
// - Kanvas dirender lalu dipetakan ke setiap LED sesuai "layout" dari editor
// - OTA lewat WiFi AP + halaman web bawaan perangkat (wajib, dengan progres %)
// - Rollback otomatis jika firmware baru crash berulang

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <FastLED.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <utility>
#include <vector>
#include "esp_ota_ops.h"
#include "config.h"
#include "web_ui.h"

#define SERIAL_BAUD    921600
#define MAX_PAYLOAD    40000UL
#define MAX_ANIM_BYTES 30000UL
#define FRAME_RATE_MS 16
#define WELCOME_MS    3600UL
#define STABLE_MS     30000UL
#define MAX_TRIES     3
#define NO_CELL       0xFFFF

// ---- kode perintah dari editor (host -> perangkat) ----
enum Cmd : uint8_t {
  C_HELLO = 0x01, C_LAYOUT = 0x02, C_LIVE = 0x03, C_SAVE = 0x04, C_PLAY = 0x05,
  C_LIST = 0x06, C_DELETE = 0x07, C_PRESET = 0x08, C_WELCOME = 0x09, C_OFF = 0x0A,
  C_BRIGHT = 0x0B, C_GET = 0x0C
};
// ---- kode balasan (perangkat -> host) ----
enum Rsp : uint8_t { R_STATUS = 0x81, R_LIST = 0x82, R_ANIM = 0x83, R_MSG = 0x8E };

enum Mode : uint8_t { M_PRESET = 0, M_LIVE, M_FILE, M_WELCOME, M_OFF };

/* ---------------- state ---------------- */
CRGB stripBuf[MAX_STRIPS][MAX_LEDS_PER_STRIP];

uint16_t layoutW = 16, layoutH = 16;
uint8_t stripCount = 1;
uint16_t stripLen[MAX_STRIPS] = {256};
std::vector<uint16_t> chainMap;      // urutan LED (berurutan antar strip) -> indeks kanvas
std::vector<uint8_t> canvas;         // kanvas RGB, W*H*3

struct Anim {
  uint8_t w = 0, h = 0, fps = 8;
  uint16_t frames = 0;
  std::vector<uint8_t> px;
};
Anim liveAnim, fileAnim;

Mode mode = M_PRESET, prevMode = M_PRESET;
uint8_t presetId = 0;
uint8_t bright = 120;
String fileName = "";
unsigned long welcomeStart = 0;
bool settingsDirty = false;
unsigned long lastBrightChange = 0;
const char* PRESET_NAMES[] = {"Running dot", "Scanner", "Kedip merah", "Stop chase",
                              "Pelangi", "Breathing rem", "Sparkle"};
const uint8_t PRESET_COUNT = 7;

// serial parser
enum RxState : uint8_t { RX_SYNC, RX_TYPE, RX_LEN1, RX_LEN2, RX_PAYLOAD, RX_CRC };
RxState rxState = RX_SYNC;
uint8_t rxType = 0, rxCrc = 0;
uint32_t rxLen = 0;
std::vector<uint8_t> rxBuf;

// OTA
enum OtaState : uint8_t { OTA_IDLE = 0, OTA_RUNNING, OTA_OK, OTA_FAIL };
volatile OtaState otaState = OTA_IDLE;
bool otaAuthFail = false, otaFirstChunk = true;
size_t otaReceived = 0;
String otaMsg = "";
unsigned long rebootAt = 0;
bool bootPending = false;
String apName;
WebServer http(80);

/* ---------------- util ---------------- */
bool validName(const String& n) {
  if (n.length() < 1 || n.length() > 24) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n.charAt(i);
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

const char* modeName() {
  switch (mode) {
    case M_PRESET:  return "preset";
    case M_LIVE:    return "live";
    case M_FILE:    return "file";
    case M_WELCOME: return "welcome";
    default:        return "off";
  }
}

// Format: "SLAN" + w(1) + h(1) + frames(2 LE) + fps(1) + reserved(1) + RGB per frame
bool parseAnim(const uint8_t* d, size_t n, Anim& a) {
  if (n < 10 || memcmp(d, "SLAN", 4) != 0) return false;
  uint8_t w = d[4], h = d[5];
  uint16_t fr = d[6] | (d[7] << 8);
  uint8_t fps = d[8];
  if (w != layoutW || h != layoutH || fr == 0 || fps == 0) return false;
  if (fps > 60) fps = 60;
  size_t dataLen = n - 10;
  if (dataLen > MAX_ANIM_BYTES) return false;
  if (dataLen != (size_t)w * h * 3 * fr) return false;
  a.w = w; a.h = h; a.frames = fr; a.fps = fps;
  a.px.assign(d + 10, d + n);
  return true;
}

void applyBrightness() {
  FastLED.setBrightness(bright > 255 ? 255 : bright);
}

/* ---------------- penyimpanan ---------------- */
void saveSettings() {
  Preferences p;
  p.begin("st", false);
  if (mode == M_PRESET || mode == M_FILE) p.putUChar("mode", (uint8_t)mode);
  p.putUChar("preset", presetId);
  p.putUChar("bright", bright);
  p.putString("file", fileName);
  p.end();
  settingsDirty = false;
}

bool playFileSafe(const String& name);

void loadSettings() {
  Preferences p;
  p.begin("st", true);
  uint8_t m = p.getUChar("mode", M_PRESET);
  presetId = p.getUChar("preset", 0);
  bright = p.getUChar("bright", 120);
  String fn = p.getString("file", "");
  p.end();
  if (presetId >= PRESET_COUNT) presetId = 0;
  if (bright < 5) bright = 120;
  mode = (Mode)m;
  if (mode == M_FILE && !(fn.length() && playFileSafe(fn))) mode = M_PRESET;
  if (mode != M_PRESET && mode != M_FILE) mode = M_PRESET;
}

// Layout disimpan dalam format payload C_LAYOUT
void setDefaultLayout() {
  layoutW = 16; layoutH = 16; stripCount = 1;
  for (int i = 0; i < MAX_STRIPS; i++) stripLen[i] = 0;
  stripLen[0] = 256;
  chainMap.clear();
  for (uint16_t y = 0; y < layoutH; y++)  // zigzag baris
    for (uint16_t i = 0; i < layoutW; i++) {
      uint16_t x = (y & 1) ? layoutW - 1 - i : i;
      chainMap.push_back(y * layoutW + x);
    }
  canvas.assign((size_t)layoutW * layoutH * 3, 0);
}

// payload: W(2) H(2) nStrips(1) len[nStrips](2 each) map[total](2 each)
bool applyLayout(const uint8_t* p, size_t n, bool save) {
  if (n < 5) return false;
  uint16_t w = p[0] | (p[1] << 8), h = p[2] | (p[3] << 8);
  uint8_t ns = p[4];
  if (w == 0 || h == 0 || (uint32_t)w * h > MAX_CANVAS_CELLS) return false;
  if (ns == 0 || ns > MAX_STRIPS) return false;
  size_t pos = 5;
  if (n < pos + ns * 2) return false;
  uint16_t lens[MAX_STRIPS] = {0};
  uint32_t total = 0;
  for (uint8_t i = 0; i < ns; i++) {
    lens[i] = p[pos] | (p[pos + 1] << 8);
    pos += 2;
    if (lens[i] > MAX_LEDS_PER_STRIP) return false;
    total += lens[i];
  }
  if (n != pos + total * 2) return false;
  std::vector<uint16_t> map(total);
  for (uint32_t k = 0; k < total; k++) {
    uint16_t v = p[pos] | (p[pos + 1] << 8);
    pos += 2;
    if (v != NO_CELL && v >= (uint32_t)w * h) return false;
    map[k] = v;
  }
  layoutW = w; layoutH = h; stripCount = ns;
  for (int i = 0; i < MAX_STRIPS; i++) stripLen[i] = (i < ns) ? lens[i] : 0;
  chainMap = std::move(map);
  canvas.assign((size_t)w * h * 3, 0);
  for (int s = 0; s < MAX_STRIPS; s++) fill_solid(stripBuf[s], MAX_LEDS_PER_STRIP, CRGB::Black);
  if (save) {
    File f = LittleFS.open("/layout.bin", FILE_WRITE);
    if (!f) return false;
    size_t wr = f.write(p, n);
    f.close();
    if (wr != n) return false;
  }
  return true;
}

void loadLayout() {
  setDefaultLayout();
  if (!LittleFS.exists("/layout.bin")) return;
  File f = LittleFS.open("/layout.bin", "r");
  if (!f) return;
  size_t n = f.size();
  std::vector<uint8_t> buf(n);
  size_t got = f.read(buf.data(), n);
  f.close();
  if (got == n) {
    if (!applyLayout(buf.data(), n, false)) setDefaultLayout();
  }
}

/* ---------------- rollback / boot guard ---------------- */
void bootGuard() {
  Preferences p;
  p.begin("boot", false);
  bootPending = p.getBool("pend", false);
  if (bootPending) {
    uint8_t tries = p.getUChar("tries", 0) + 1;
    if (tries > MAX_TRIES) {
      p.putBool("pend", false);
      p.putUChar("tries", 0);
      p.end();
      const esp_partition_t* old = esp_ota_get_next_update_partition(NULL);
      if (old && esp_ota_set_boot_partition(old) == ESP_OK) ESP.restart();
      return;
    }
    p.putUChar("tries", tries);
  }
  p.end();
}

/* ---------------- kanvas & render ---------------- */
inline void setPx(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || y < 0 || x >= layoutW || y >= layoutH) return;
  uint8_t* p = &canvas[((size_t)y * layoutW + x) * 3];
  p[0] = r; p[1] = g; p[2] = b;
}
void fillCanvas(uint8_t r, uint8_t g, uint8_t b) {
  for (size_t i = 0; i < canvas.size(); i += 3) { canvas[i] = r; canvas[i + 1] = g; canvas[i + 2] = b; }
}

void renderAnimToCanvas(const Anim& a, unsigned long now) {
  uint64_t tick = (uint64_t)now * a.fps / 1000ULL;
  uint16_t fi = tick % a.frames;
  size_t fb = (size_t)layoutW * layoutH * 3;
  memcpy(canvas.data(), a.px.data() + (size_t)fi * fb, fb);
}

void renderPreset(uint8_t id, unsigned long t) {
  const int W = layoutW, H = layoutH, N = W * H;
  fillCanvas(0, 0, 0);
  switch (id) {
    case 0: {  // running dot (berjalan baris demi baris)
      int k = (t / 60) % N;
      setPx(k % W, k / W, 255, 0, 0);
      break;
    }
    case 1: {  // scanner kolom bolak-balik
      int span = W > 1 ? 2 * (W - 1) : 1;
      int s = (t / 60) % span;
      int pos = s < W ? s : span - s;
      for (int x = 0; x < W; x++) {
        int d = abs(x - pos);
        uint8_t b = d == 0 ? 255 : d == 1 ? 90 : d == 2 ? 25 : 0;
        for (int y = 0; y < H; y++) setPx(x, y, b, 0, 0);
      }
      break;
    }
    case 2: {  // kedip merah
      if (((t / 500) & 1) == 0) fillCanvas(255, 0, 0);
      break;
    }
    case 3: {  // stop chase per baris: merah, kuning, hijau
      uint8_t ph = (t / 400) % 3;
      for (int y = 0; y < H; y++) {
        uint8_t k = (y + ph) % 3;
        uint8_t r = k == 2 ? 0 : 255, g = k == 0 ? 0 : (k == 1 ? 120 : 255), b = 0;
        if (k == 2) g = 255;
        for (int x = 0; x < W; x++) setPx(x, y, r, g, b);
      }
      break;
    }
    case 4: {  // pelangi
      uint8_t base = t / 25;
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          CRGB c;
          hsv2rgb_rainbow(CHSV((uint8_t)(x * 8 + y * 4 + base), 255, 255), c);
          setPx(x, y, c.r, c.g, c.b);
        }
      break;
    }
    case 5: {  // breathing merah (lampu rem)
      uint8_t b = beatsin8(30, 40, 255);
      fillCanvas(b, 0, 0);
      break;
    }
    case 6: {  // sparkle acak
      for (int i = 0; i < N; i++)
        if (random8() < 20) setPx(i % W, i / W, 255, 255, 255);
      break;
    }
    default:
      break;
  }
}

// Welcome ala mobil Eropa: sapuan tepi ke tengah, tahan, lalu fade
void renderWelcome(unsigned long t) {
  fillCanvas(0, 0, 0);
  float sweep = t < 1200 ? t / 1200.0f : 1.0f;
  long fade = 255;
  if (t > 2400) fade = constrain(255 - (long)(t - 2400) * 255 / 1200, 0L, 255L);
  uint8_t r = (uint8_t)((255 * fade) / 255);
  for (int x = 0; x < layoutW; x++) {
    int edge = min(x, layoutW - 1 - x);
    bool on = edge < sweep * (layoutW / 2.0f);
    if (on) for (int y = 0; y < layoutH; y++) setPx(x, y, r, 0, 0);
  }
}

void startWelcome() {
  if (mode != M_WELCOME) prevMode = mode;
  welcomeStart = millis();
  mode = M_WELCOME;
}

void renderCanvas(unsigned long now) {
  switch (mode) {
    case M_PRESET: renderPreset(presetId, now); break;
    case M_LIVE:
      if (!liveAnim.px.empty()) renderAnimToCanvas(liveAnim, now);
      else fillCanvas(0, 0, 0);
      break;
    case M_FILE:
      if (!fileAnim.px.empty()) renderAnimToCanvas(fileAnim, now);
      else fillCanvas(0, 0, 0);
      break;
    case M_WELCOME: {
      unsigned long el = now - welcomeStart;
      if (el >= WELCOME_MS) { mode = prevMode; fillCanvas(0, 0, 0); }
      else renderWelcome(el);
      break;
    }
    default: fillCanvas(0, 0, 0);
  }
}

// kanvas -> LED semua strip mengikuti chainMap
void pushToStrips() {
  size_t k = 0;
  for (uint8_t s = 0; s < stripCount; s++) {
    for (uint16_t j = 0; j < stripLen[s]; j++, k++) {
      uint16_t ci = k < chainMap.size() ? chainMap[k] : NO_CELL;
      CRGB c = CRGB::Black;
      if (ci != NO_CELL) {
        const uint8_t* p = &canvas[(size_t)ci * 3];
        c = CRGB(p[0], p[1], p[2]);
      }
      stripBuf[s][j] = c;
    }
  }
}

/* ---------------- penyimpanan animasi ---------------- */
bool playFileSafe(const String& name) {
  if (!validName(name)) return false;
  File f = LittleFS.open(String("/anim/") + name + ".bin", "r");
  if (!f) return false;
  size_t n = f.size();
  if (n > MAX_ANIM_BYTES + 10) { f.close(); return false; }
  std::vector<uint8_t> buf(n);
  size_t got = f.read(buf.data(), n);
  f.close();
  if (got != n) return false;
  Anim a;
  if (!parseAnim(buf.data(), n, a)) return false;
  fileAnim = std::move(a);
  fileName = name;
  mode = M_FILE;
  return true;
}

String listJson() {
  String j = "{\"items\":[";
  File root = LittleFS.open("/anim");
  bool first = true;
  if (root && root.isDirectory()) {
    File f = root.openNextFile();
    while (f) {
      String n = f.name();
      int sl = n.lastIndexOf('/');
      if (sl >= 0) n = n.substring(sl + 1);
      if (n.endsWith(".bin")) {
        n.remove(n.length() - 4);
        if (!first) j += ",";
        first = false;
        j += "\""; j += n; j += "\"";
      }
      f.close();
      f = root.openNextFile();
    }
  }
  j += "]}";
  return j;
}

String statusJson() {
  String j = "{\"fw\":\""; j += FW_VERSION;
  j += "\",\"mode\":\""; j += modeName();
  j += "\",\"preset\":"; j += presetId;
  j += ",\"file\":\""; j += fileName;
  j += "\",\"bright\":"; j += bright;
  j += ",\"w\":"; j += layoutW;
  j += ",\"h\":"; j += layoutH;
  j += ",\"strips\":"; j += stripCount;
  j += ",\"presets\":[";
  for (uint8_t i = 0; i < PRESET_COUNT; i++) {
    if (i) j += ",";
    j += "\""; j += PRESET_NAMES[i]; j += "\"";
  }
  j += "]}";
  return j;
}

/* ---------------- protokol serial ---------------- */
void sendFrame(uint8_t type, const uint8_t* data, size_t len) {
  uint8_t head[4] = {0xA5, type, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
  uint8_t crc = 0;
  for (int i = 1; i < 4; i++) crc ^= head[i];
  for (size_t i = 0; i < len; i++) crc ^= data[i];
  Serial.write(head, 4);
  if (len) Serial.write(data, len);
  Serial.write(&crc, 1);
}
void sendText(uint8_t type, const String& s) { sendFrame(type, (const uint8_t*)s.c_str(), s.length()); }
void sendAck(bool ok, const String& msg) { sendText(R_MSG, String(ok ? "O" : "E") + msg); }
void sendStatus() { sendText(R_STATUS, statusJson()); }

void handleCmd(uint8_t type, const std::vector<uint8_t>& p) {
  switch (type) {
    case C_HELLO:
      sendStatus();
      break;

    case C_LAYOUT:
      if (applyLayout(p.data(), p.size(), true)) {
        // animasi lama yang ukurannya tidak cocok dihapus dari RAM
        if (mode == M_LIVE) { liveAnim = Anim(); mode = M_OFF; }
        if (mode == M_FILE) { fileAnim = Anim(); mode = M_PRESET; }
        sendAck(true, "Layout disimpan");
      } else {
        sendAck(false, "Layout tidak valid");
      }
      sendStatus();
      break;

    case C_LIVE: {
      Anim tmp;
      if (parseAnim(p.data(), p.size(), tmp)) {
        liveAnim = std::move(tmp);
        mode = M_LIVE;
      } else {
        sendAck(false, "Animasi tidak cocok dengan layout");
      }
      break;
    }

    case C_SAVE: {
      if (p.size() < 1) { sendAck(false, "Paket rusak"); break; }
      uint8_t nl = p[0];
      if (p.size() < (size_t)1 + nl + 10) { sendAck(false, "Paket rusak"); break; }
      String name = "";
      for (uint8_t i = 0; i < nl; i++) name += (char)p[1 + i];
      const uint8_t* data = p.data() + 1 + nl;
      size_t dlen = p.size() - 1 - nl;
      Anim tmp;
      if (!validName(name)) { sendAck(false, "Nama tidak valid"); break; }
      if (!parseAnim(data, dlen, tmp)) { sendAck(false, "Animasi tidak cocok dengan layout"); break; }
      String path = String("/anim/") + name + ".bin";
      File f = LittleFS.open(path, FILE_WRITE);
      if (!f) { sendAck(false, "Gagal membuka penyimpanan"); break; }
      size_t wr = f.write(data, dlen);
      f.close();
      if (wr != dlen) { LittleFS.remove(path); sendAck(false, "Penyimpanan penuh"); break; }
      sendAck(true, "Tersimpan: " + name);
      sendText(R_LIST, listJson());
      break;
    }

    case C_PLAY: {
      String n = "";
      for (uint8_t b : p) n += (char)b;
      if (playFileSafe(n)) {
        fileName = n; settingsDirty = true; saveSettings();
        sendAck(true, "Diputar: " + n);
      } else {
        sendAck(false, "Animasi tidak ditemukan atau tidak cocok");
      }
      break;
    }

    case C_LIST:
      sendText(R_LIST, listJson());
      break;

    case C_DELETE: {
      String n = "";
      for (uint8_t b : p) n += (char)b;
      if (validName(n) && LittleFS.remove(String("/anim/") + n + ".bin")) {
        if (fileName == n) { fileName = ""; if (mode == M_FILE) mode = M_PRESET; settingsDirty = true; saveSettings(); }
        sendAck(true, "Dihapus: " + n);
        sendText(R_LIST, listJson());
      } else {
        sendAck(false, "Gagal menghapus");
      }
      break;
    }

    case C_PRESET:
      if (p.size() >= 1) {
        presetId = p[0] < PRESET_COUNT ? p[0] : 0;
        mode = M_PRESET;
        settingsDirty = true; saveSettings();
      }
      break;

    case C_WELCOME:
      startWelcome();
      break;

    case C_OFF:
      mode = M_OFF;
      break;

    case C_BRIGHT:
      if (p.size() >= 1) {
        bright = p[0] < 5 ? 5 : p[0];
        applyBrightness();
        settingsDirty = true; lastBrightChange = millis();
      }
      break;

    case C_GET: {
      String n = "";
      for (uint8_t b : p) n += (char)b;
      File f = validName(n) ? LittleFS.open(String("/anim/") + n + ".bin", "r") : File();
      if (!f) { sendAck(false, "Animasi tidak ditemukan"); break; }
      size_t sz = f.size();
      std::vector<uint8_t> buf(sz);
      size_t got = f.read(buf.data(), sz);
      f.close();
      if (got == sz) sendFrame(R_ANIM, buf.data(), sz);
      else sendAck(false, "Gagal membaca animasi");
      break;
    }

    default:
      sendAck(false, "Perintah tidak dikenal");
  }
}

// pembaca paket: 0xA5 | type | len(2 LE) | payload | crc (xor type..payload)
void serialPoll() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    switch (rxState) {
      case RX_SYNC:
        if (b == 0xA5) rxState = RX_TYPE;
        break;
      case RX_TYPE:
        rxType = b; rxCrc = b; rxState = RX_LEN1;
        break;
      case RX_LEN1:
        rxLen = b; rxCrc ^= b; rxState = RX_LEN2;
        break;
      case RX_LEN2:
        rxLen |= (uint32_t)b << 8; rxCrc ^= b;
        rxBuf.clear();
        if (rxLen > MAX_PAYLOAD) { rxState = RX_SYNC; sendAck(false, "Paket terlalu besar"); }
        else rxState = rxLen ? RX_PAYLOAD : RX_CRC;
        break;
      case RX_PAYLOAD:
        rxBuf.push_back(b); rxCrc ^= b;
        if (rxBuf.size() >= rxLen) rxState = RX_CRC;
        break;
      case RX_CRC:
        if (b == rxCrc) handleCmd(rxType, rxBuf);
        else sendAck(false, "CRC salah, paket dibuang");
        rxState = RX_SYNC;
        break;
    }
  }
}

/* ---------------- OTA lewat halaman web perangkat ---------------- */
void failOta(const String& msg) {
  Update.abort();
  otaState = OTA_FAIL;
  otaMsg = msg;
}

void onOtaUpload() {
  HTTPUpload& up = http.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      otaAuthFail = !http.authenticate(OTA_USER, AP_PASS);
      if (otaAuthFail) return;
      if (otaState == OTA_RUNNING) { otaAuthFail = true; return; }
      otaState = OTA_RUNNING; otaMsg = ""; otaReceived = 0; otaFirstChunk = true;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) failOta(Update.errorString());
      break;
    case UPLOAD_FILE_WRITE:
      if (otaAuthFail || otaState != OTA_RUNNING || up.currentSize == 0) break;
      if (otaFirstChunk) {
        otaFirstChunk = false;
        if (up.buf[0] != 0xE9) { failOta("File bukan firmware ESP32"); break; }
      }
      if (Update.write(up.buf, up.currentSize) != up.currentSize) { failOta(Update.errorString()); break; }
      otaReceived += up.currentSize;
      break;
    case UPLOAD_FILE_END:
      if (otaAuthFail || otaState != OTA_RUNNING) break;
      if (otaReceived == 0) { failOta("File kosong"); break; }
      if (Update.end(true)) { otaState = OTA_OK; otaMsg = "OK"; }
      else failOta(Update.errorString());
      break;
    case UPLOAD_FILE_ABORTED:
      failOta("Upload terputus. Firmware lama tetap aman.");
      break;
    default:
      break;
  }
}

void onOtaDone() {
  if (otaAuthFail) {
    otaAuthFail = false;
    http.requestAuthentication();
    return;
  }
  if (otaState == OTA_OK) {
    http.send(200, "application/json", "{\"ok\":true,\"msg\":\"Update berhasil, perangkat restart\"}");
    Preferences p;
    p.begin("boot", false);
    p.putBool("pend", true);
    p.putUChar("tries", 0);
    p.end();
    rebootAt = millis() + 1500;
  } else {
    http.send(500, "application/json", String("{\"ok\":false,\"msg\":\"") + otaMsg + "\"}");
    otaState = OTA_IDLE;
  }
}

void setupHttp() {
  http.on("/", HTTP_GET, []() { http.send_P(200, "text/html", OTA_PAGE_HTML); });
  http.on("/api/status", HTTP_GET, []() { http.send(200, "application/json", statusJson()); });
  http.on("/api/ota", HTTP_POST, onOtaDone, onOtaUpload);
  http.onNotFound([]() { http.send(404, "text/plain", "not found"); });
  http.begin();
}

/* ---------------- setup & loop ---------------- */
// Satu controller FastLED per pin. Pin harus konstanta waktu compile (dari config.h).
template <int PIN>
void addStrip(CRGB* buf) {
  FastLED.addLeds<LED_CHIPSET, PIN, COLOR_ORDER>(buf, MAX_LEDS_PER_STRIP);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  bootGuard();

  if (!LittleFS.begin(true)) { /* penyimpanan gagal: layout & animasi tidak tersedia */ }
  if (!LittleFS.exists("/anim")) LittleFS.mkdir("/anim");
  loadLayout();
  loadSettings();

  addStrip<STRIP_PIN_0>(stripBuf[0]);
  addStrip<STRIP_PIN_1>(stripBuf[1]);
  addStrip<STRIP_PIN_2>(stripBuf[2]);
  addStrip<STRIP_PIN_3>(stripBuf[3]);
  addStrip<STRIP_PIN_4>(stripBuf[4]);
  addStrip<STRIP_PIN_5>(stripBuf[5]);
  addStrip<STRIP_PIN_6>(stripBuf[6]);
  addStrip<STRIP_PIN_7>(stripBuf[7]);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  applyBrightness();
  for (int s = 0; s < MAX_STRIPS; s++) fill_solid(stripBuf[s], MAX_LEDS_PER_STRIP, CRGB::Black);
  FastLED.show();

  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "%s-%04X", AP_PREFIX, (unsigned)(mac & 0xFFFF));
  apName = buf;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str(), AP_PASS, 1, false, 2);
  setupHttp();

  startWelcome();
}

void loop() {
  serialPoll();
  http.handleClient();
  unsigned long now = millis();

  if (rebootAt && now >= rebootAt) ESP.restart();

  static unsigned long lastFrame = 0;
  if (otaState != OTA_RUNNING && now - lastFrame >= FRAME_RATE_MS) {
    lastFrame = now;
    renderCanvas(now);
    pushToStrips();
    FastLED.show();
  }

  if (bootPending && now >= STABLE_MS) {
    Preferences p;
    p.begin("boot", false);
    p.putBool("pend", false);
    p.putUChar("tries", 0);
    p.end();
    esp_ota_mark_app_valid_cancel_rollback();
    bootPending = false;
  }

  if (settingsDirty && now - lastBrightChange > 2000) saveSettings();
}
