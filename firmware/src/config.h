// Konfigurasi build firmware. Target: ESP32-C3 (USB-C built-in).
// Panjang per strip, ukuran kanvas, dan susunan diatur dari editor lewat serial, tanpa compile ulang.
// Pin strip ditetapkan di sini (dipilih saat compile).
#pragma once

// ---- pin data tiap strip ----
// Jumlah strip = MAX_STRIPS. Tambah/ganti pin di daftar ini (maks 8 di firmware ini).
// ESP32-C3: hindari GPIO 18 dan 19 (dipakai USB), GPIO 2, 8, 9 (strapping boot).
#define MAX_STRIPS          8
#define STRIP_PIN_0         0
#define STRIP_PIN_1         1
#define STRIP_PIN_2         3
#define STRIP_PIN_3         4
#define STRIP_PIN_4         5
#define STRIP_PIN_5         6
#define STRIP_PIN_6         7
#define STRIP_PIN_7         10

// ---- LED ----
#define MAX_LEDS_PER_STRIP  300      // batas LED per strip (menentukan waktu refresh)
#define MAX_CANVAS_CELLS    4096     // maks. dot pada kanvas (mis. 64x64)
#define LED_CHIPSET         WS2812B  // juga cocok: SK6812 RGB (timing sama, urutan GRB)
#define COLOR_ORDER         GRB
#define MAX_MILLIAMPS       3000     // batas arus total FastLED (sesuaikan dengan step-down)

// ---- WiFi AP untuk halaman OTA ----
#define AP_PREFIX           "StopLamp"
#define AP_PASS             "stoplamp123"   // min 8 karakter
#define OTA_USER            "admin"
#define FW_VERSION          "2.1.0"
