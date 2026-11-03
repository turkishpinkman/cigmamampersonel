#pragma once

#include <Arduino.h>

// ==========================================
// PIN TANIMLARI
// ==========================================

// Ekran (ILI9341) - SPI
#define TFT_CS    14
#define TFT_DC    27
#define TFT_RST   26

// Dokunmatik (XPT2046)
#define TOUCH_CS  25
#define TOUCH_IRQ 34

// NFC (PN532) - I2C
#define NFC_SDA   21
#define NFC_SCL   22

// Buzzer
#define BUZZER_PIN     32
#define BUZZER_CHANNEL 0
#define BUZZER_FREQ    2000

// ==========================================
// DOKUNMATIK KALIBRASYON
// ==========================================
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

// ==========================================
// SISTEM SABITLERI
// ==========================================
#define DEBOUNCE_MS         3000
#define SELECTION_TIMEOUT   5000
#define INFO_DISPLAY_MS     2500
#define NTP_SYNC_INTERVAL   900000
#define TIME_SAVE_INTERVAL  3600000
#define WIFI_RETRY_INTERVAL 5000
#define MAX_EMPLOYEES       50
#define MAX_PENDING_EVENTS  500
#define SHIFT_END_TOLERANCE 30
#define SHIFT_START_EARLY_TOLERANCE 30  // Mesai baslamadan en fazla kac dk once girise izin verilecek
#define MAIN_SCREEN_REFRESH 5000

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// ==========================================
// VARSAYILAN AYARLAR
// ==========================================
#define DEFAULT_SHIFT_START_H  8
#define DEFAULT_SHIFT_START_M  0
#define DEFAULT_SHIFT_END_H    17
#define DEFAULT_SHIFT_END_M    0

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID      "G514"
#endif

#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS      "jkaf4tpn9g"
#endif

#ifndef DEFAULT_ADMIN_UID
#define DEFAULT_ADMIN_UID      "BB:B9:27:0B"
#endif

#ifndef OTA_ADMIN_USER
#define OTA_ADMIN_USER         "admin"
#endif

#ifndef OTA_ADMIN_PASS
#define OTA_ADMIN_PASS         "cigmamam2026"
#endif

// ==========================================
// NTP AYARLARI
// ==========================================
#define NTP_SERVER   "pool.ntp.org"
#define GMT_OFFSET   10800
#define DST_OFFSET   0

// ==========================================
// LittleFS DOSYA YOLLARI
// ==========================================
#define PATH_CONFIG     "/config.json"
#define PATH_EMPLOYEES  "/employees.json"
#define PATH_STATES     "/states.json"
#define PATH_TIME       "/time.json"
#define PATH_EVENTS     "/events.json"

// ==========================================
// EKRAN BOYUTLARI
// ==========================================
#define SCREEN_W 320
#define SCREEN_H 240

// ==========================================
// RENK TANIMLARI (RGB565)
// ==========================================
#define COLOR_BG         0x0000
#define COLOR_TEXT        0xFFFF
#define COLOR_HEADER_BG  0x001F
#define COLOR_SUCCESS    0x07E0
#define COLOR_ERROR      0xF800
#define COLOR_WARNING    0xFFE0
#define COLOR_MOLA_BTN   0xFD20
#define COLOR_CIKIS_BTN  0xF800
#define COLOR_ADMIN_BTN  0x001F
#define COLOR_GRAY       0x7BEF
#define COLOR_DARK_GRAY  0x4208
#define COLOR_LIGHT_BLUE 0x34DF
#define COLOR_GREEN_DARK 0x0400
#define COLOR_CARD_BG    0x10A2

// ==========================================
// ENUM TANIMLARI
// ==========================================

enum EmployeeState : uint8_t {
  STATE_DISARIDA  = 0,
  STATE_CALISIYOR = 1,
  STATE_MOLADA    = 2
};

enum EventType : uint8_t {
  EVT_GIRIS     = 0,
  EVT_CIKIS     = 1,
  EVT_MOLA_BAS  = 2,
  EVT_MOLA_BIT  = 3,
  EVT_OTO_CIKIS = 4
};

enum ScreenState : uint8_t {
  SCR_MAIN          = 0,
  SCR_SELECTION     = 1,
  SCR_INFO          = 2,
  SCR_ADMIN         = 3,
  SCR_ADMIN_ADD     = 4,
  SCR_ADMIN_LIST    = 5,
  SCR_ADMIN_SHIFT   = 6,
  SCR_ADMIN_DELETE  = 7,
  SCR_KEYBOARD      = 8,
  SCR_REGISTER_CARD = 9   // Hizli personel kayit ekrani
};

// ==========================================
// FIREBASE AYARLARI
// ==========================================
#define FIREBASE_API_KEY      "AIzaSyA_uB2Q4gtNO8wnuKawx_gfhiNVGquQ95s"
#define FIREBASE_PROJECT_ID   "cigmamam-personel"
// Cihaz kimlik dogrulama (Firebase Auth)
#define FIREBASE_DEVICE_EMAIL "device@cigmamam.com"
#ifndef FIREBASE_DEVICE_PASS
#define FIREBASE_DEVICE_PASS  "CigmamamDevice2026!"
#endif
#define FIREBASE_DEVICE_ID    "ESP32_001"

// Senkronizasyon araliklari (ms)
#define SYNC_EVENT_INTERVAL     15000    // 15 saniye - olay yukleme kontrolu
#define SYNC_EMPLOYEE_INTERVAL  300000   // 5 dakika - personel indirme
#define SYNC_SETTINGS_INTERVAL  60000    // 1 dakika - ayar ve firmware guncelleme kontrolu
#define SYNC_BATCH_SIZE         5        // Her dongude yuklenecek maksimum olay sayisi (WDT korumasi)

// Mevcut cihaz yazilim surumu
#define CURRENT_FIRMWARE_VERSION 111     // v1.1.1 (OTA surum takip numarasi)

// ==========================================
// VERI YAPILARI
// ==========================================

struct ShiftConfig {
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t endHour;
  uint8_t endMinute;
};

struct Employee {
  char uid[24];
  char name[32];
  EmployeeState state;
  unsigned long lastEventTime;
  unsigned long breakStartTime;
  int breakCount;
  bool active;
  bool hasCustomShift;
  ShiftConfig customShift;
};

struct LogEvent {
  unsigned long id;
  char uid[24];
  char name[32];
  EventType type;
  unsigned long timestamp;
  char date[12];
  bool synced;
};

struct SystemConfig {
  char wifiSSID[33];
  char wifiPass[65];
  char adminUID[24];
  ShiftConfig shift;
};

// ==========================================
// YARDIMCI FONKSIYONLAR
// ==========================================

inline const char* eventTypeToStr(EventType t) {
  switch(t) {
    case EVT_GIRIS:     return "GIRIS";
    case EVT_CIKIS:     return "CIKIS";
    case EVT_MOLA_BAS:  return "MOLA_BAS";
    case EVT_MOLA_BIT:  return "MOLA_BIT";
    case EVT_OTO_CIKIS: return "OTO_CIKIS";
    default:            return "BILINMEYEN";
  }
}

inline const char* stateToStr(EmployeeState s) {
  switch(s) {
    case STATE_DISARIDA:  return "DISARIDA";
    case STATE_CALISIYOR: return "CALISIYOR";
    case STATE_MOLADA:    return "MOLADA";
    default:              return "BILINMEYEN";
  }
}

inline void jsonEscape(const char* in, char* out, size_t maxLen) {
  if (!in || !out || maxLen == 0) return;
  size_t j = 0;
  for (size_t i = 0; in[i] != '\0' && j < maxLen - 1; i++) {
    char c = in[i];
    if (c == '"' || c == '\\') {
      if (j + 2 >= maxLen) break;
      out[j++] = '\\';
      out[j++] = c;
    } else if (c == '\n') {
      if (j + 2 >= maxLen) break;
      out[j++] = '\\';
      out[j++] = 'n';
    } else if (c == '\r') {
      if (j + 2 >= maxLen) break;
      out[j++] = '\\';
      out[j++] = 'r';
    } else if (c == '\t') {
      if (j + 2 >= maxLen) break;
      out[j++] = '\\';
      out[j++] = 't';
    } else if ((unsigned char)c >= 32) {
      out[j++] = c;
    }
  }
  out[j] = '\0';
}

inline void normalizeUID(const char* in, char* out, size_t maxLen) {
  if (!in || !out || maxLen == 0) return;
  size_t j = 0;
  for (size_t i = 0; in[i] != '\0' && j < maxLen - 1; i++) {
    if (in[i] != ':' && in[i] != ' ' && in[i] != '-') {
      out[j++] = toupper((unsigned char)in[i]);
    }
  }
  out[j] = '\0';
}

inline bool uidMatch(const char* a, const char* b) {
  if (!a || !b) return false;
  char normA[32];
  char normB[32];
  normalizeUID(a, normA, sizeof(normA));
  normalizeUID(b, normB, sizeof(normB));
  return (strcmp(normA, normB) == 0);
}
