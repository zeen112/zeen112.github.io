// Konfigurasi build firmware. Pin strip ditetapkan di sini (dipilih saat compile).
// Panjang per strip, ukuran kanvas, dan susunan diatur dari editor lewat serial, tanpa compile ulang.
#pragma once

// ---- pin data tiap strip (hindari pin 6-11 flash, 34-39 input-only) ----
#define MAX_STRIPS          4
#define STRIP_PIN_0         16
#define STRIP_PIN_1         17
#define STRIP_PIN_2         18
#define STRIP_PIN_3         19

// ---- LED ----
#define MAX_LEDS_PER_STRIP  256      // batas jumlah LED per strip (menentukan kecepatan refresh)
#define MAX_CANVAS_CELLS    4096     // maks. dot pada kanvas (mis. 64x64)
#define LED_CHIPSET         WS2812B  // juga cocok: SK6812 RGB (timing sama, urutan GRB)
#define COLOR_ORDER         GRB
#define MAX_MILLIAMPS       3000     // batas arus total FastLED (sesuaikan dengan step-down)

// ---- WiFi AP untuk halaman OTA ----
#define AP_PREFIX           "StopLamp"
#define AP_PASS             "stoplamp123"   // min 8 karakter
#define OTA_USER            "admin"
#define FW_VERSION          "2.0.0"
