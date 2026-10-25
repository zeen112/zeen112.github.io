# StopLamp - Editor Animasi Lampu Dot (ESP32 + strip LED)

Dua bagian:

| Bagian | Dijalankan di | Isi |
|---|---|---|
| `editor/public/` | Vercel (static), dibuka di Chrome/Edge | Editor kanvas, layout strip, preview, simpan ke perangkat, flash via USB |
| `firmware/` | ESP32 DevKit | Menjalankan strip, menerima perintah lewat USB serial, halaman OTA sendiri lewat WiFi |

## Fitur

- **Hingga 4 strip** WS2812B (atau SK6812 RGB), masing-masing di pin GPIO tetap: 16, 17, 18, 19.
- **Panjang dan susunan bisa diubah dari editor** tanpa compile ulang: baris/kolom, zigzag atau lurus, dibalik atau tidak.
- **Kanvas dot** sampai 64×64. Animasi berjalan terus sesuai frame yang digambar, dan setiap coretan dikirim realtime ke LED.
- **Preview** lokal di browser, dan simpan/muat/hapus animasi di flash ESP32.
- **Preset**: Running dot, Scanner, Kedip merah, Stop chase, Pelangi, Breathing rem, Sparkle.
- **Welcome ala mobil Eropa**: sapuan dari tepi ke tengah, tahan, lalu fade. Diputar saat perangkat menyala.
- **Flash firmware via USB** langsung dari website (esptool-js + Web Serial).
- **OTA via WiFi** di halaman bawaan perangkat (`http://192.168.4.1` setelah tersambung ke WiFi `StopLamp-XXXX`), dengan progres persen dan login.
- **Pengaman OTA**: validasi header firmware, verifikasi image, firmware lama tidak disentuh saat upload, dan rollback otomatis jika firmware baru crash berulang.

## Arsitektur

```
Browser (Vercel)  ──USB serial 921600──▶  ESP32  ──▶  strip 1..4 (pin 16,17,18,19)
   editor                                  │
   (layout, kanvas)                        └── WiFi AP "StopLamp-XXXX" ──▶ halaman OTA (HP/laptop)
```

Editor tidak bisa mengontrol ESP32 lewat WiFi karena halaman `https` (Vercel) tidak boleh mengakses `http://192.168.4.1` atau `ws://`. Itu sebabnya kontrol harian lewat USB, dan OTA tetap lewat halaman di perangkat.

## Protokol serial

Setiap paket: `0xA5 | tipe | panjang (2 byte, little endian) | payload | CRC` dengan CRC = XOR dari tipe, panjang, dan payload.

| Tipe | Arah | Fungsi |
|---|---|---|
| 0x01 | → | HELLO (minta status) |
| 0x02 | → | LAYOUT: W, H, jumlah strip, panjang tiap strip, peta LED→sel |
| 0x03 | → | LIVE: animasi (format SLAN) diputar terus |
| 0x04 | → | SAVE: nama + animasi |
| 0x05 / 0x07 / 0x0C | → | PLAY / DELETE / GET nama |
| 0x06 | → | LIST |
| 0x08 / 0x09 / 0x0A / 0x0B | → | PRESET / WELCOME / OFF / BRIGHT |
| 0x81 / 0x82 / 0x83 / 0x8E | ← | STATUS / LIST / ANIM / pesan (O=ok, E=error) |

## Menjalankan editor

1. **Deploy ke Vercel**: impor repo, lalu atur *Output Directory* ke `editor/public`. Tidak perlu build.
2. Buka dengan Chrome atau Edge di desktop (Web Serial tidak ada di Firefox, Safari, atau iPhone).
3. Klik **Hubungkan**, pilih port ESP32.

## Build & flash firmware

Firmware tidak dibuat di Vercel. Build di laptop:

```bash
pip install platformio
./scripts/build-firmware.sh     # membuat firmware lalu menyalin 4 file ke editor/public/firmware/
```

Untuk pemasangan pertama, tombol **Flash firmware via USB** di editor bisa dipakai. Jika gagal, tahan tombol BOOT saat "Connecting…" dan tutup Serial Monitor.

## Konfigurasi

- Pin strip, batas arus, dan password WiFi: `firmware/src/config.h`.
- Panjang strip, ukuran kanvas, dan susunan: editor (bagian 1), tidak perlu compile ulang.
- Batas LED per strip (`MAX_LEDS_PER_STRIP`, default 256) menentukan kecepatan refresh: 4 strip × 256 LED ≈ 30 fps.

## Hal yang perlu diverifikasi

- **Belum dikompilasi.** Firmware belum pernah dibuild, karena registry PlatformIO tidak bisa diakses dari lingkungan pengembangan. Jalankan `pio run` dulu sebelum flash.
- **Versi esptool-js belum diverifikasi** (`editor/public/js/flash.js`, konstanta `ESPTOOL_URL`). Pastikan versi di npm ada dan API `ESPLoader.writeFlash` cocok. Versi terbaru bisa mewajibkan fungsi MD5 yang belum disediakan.
- **Belum diuji di hardware**: LED, multi-strip, serial protokol, dan OTA.
- Kanvas besar (mis. 30 KB animasi) memerlukan waktu kirim sekitar 0,3 detik lewat USB.
