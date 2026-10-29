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
#define MAIN_SCREEN_REFRESH 5000

// ==========================================
// VARSAYILAN AYARLAR
// ==========================================
#define DEFAULT_SHIFT_START_H  8
#define DEFAULT_SHIFT_START_M  0
#define DEFAULT_SHIFT_END_H    17
#define DEFAULT_SHIFT_END_M    0
#define DEFAULT_WIFI_SSID      "G514"
#define DEFAULT_WIFI_PASS      "jkaf4tpn9g"
#define DEFAULT_ADMIN_UID      "00000000"

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
#define FIREBASE_DEVICE_PASS  "CigmamamDevice2026!"
#define FIREBASE_DEVICE_ID    "ESP32_001"

// Senkronizasyon araliklari (ms)
#define SYNC_EVENT_INTERVAL     30000    // 30 saniye - olay yukleme
#define SYNC_EMPLOYEE_INTERVAL  300000   // 5 dakika - personel indirme
#define SYNC_SETTINGS_INTERVAL  60000    // 1 dakika - ayar ve firmware guncelleme kontrolu

// Mevcut cihaz yazilim surumu
#define CURRENT_FIRMWARE_VERSION 100     // v1.0.0 (ornegin 101 = v1.0.1)

// ==========================================
// VERI YAPILARI
// ==========================================

struct Employee {
  char uid[24];
  char name[32];
  EmployeeState state;
  unsigned long lastEventTime;
  unsigned long breakStartTime;
  int breakCount;
  bool active;
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

struct ShiftConfig {
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t endHour;
  uint8_t endMinute;
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
