#include "time_manager.h"
#include "storage_manager.h"
#include <WiFi.h>
#include <time.h>
#include <ArduinoJson.h>

// ==========================================
// Dahili degiskenler
// ==========================================
static unsigned long _ntpTime = 0;          // Son NTP zamani (unix timestamp)
static unsigned long _ntpMillis = 0;         // NTP sync anindaki millis() degeri
static bool _timeValid = false;
static unsigned long _lastSyncAttempt = 0;
static unsigned long _lastSave = 0;
static unsigned long _prevMillis = 0;        // millis() overflow tespiti icin

// ==========================================
// timeNow - Guncel unix timestamp hesapla
// ==========================================
unsigned long timeNow() {
  if (!_timeValid) return 0;
  
  unsigned long currentMillis = millis();
  unsigned long elapsed;
  
  // millis() overflow koruması (~49.7 gun)
  if (currentMillis >= _ntpMillis) {
    elapsed = currentMillis - _ntpMillis;
  } else {
    // Overflow oldu: 0xFFFFFFFF - _ntpMillis + currentMillis + 1
    elapsed = (0xFFFFFFFF - _ntpMillis) + currentMillis + 1;
  }
  
  return _ntpTime + (elapsed / 1000);
}

// ==========================================
// timeSyncNTP - NTP sunucusundan zaman al
// ==========================================
bool timeSyncNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ZAMAN] WiFi bagli degil, NTP sync yapilamadi");
    return false;
  }
  
  _lastSyncAttempt = millis();
  
  Serial.println("[ZAMAN] NTP senkronizasyonu baslatiliyor...");
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  
  // Maksimum 5 saniye bekle (non-blocking loop)
  struct tm timeinfo;
  unsigned long startWait = millis();
  while (!getLocalTime(&timeinfo, 1000)) {
    if (millis() - startWait > 5000) {
      Serial.println("[ZAMAN] NTP timeout!");
      return false;
    }
    delay(100);
  }
  
  _ntpTime = mktime(&timeinfo);
  _ntpMillis = millis();
  _timeValid = true;
  
  char buf[20];
  timeFormat(buf, _ntpTime);
  Serial.printf("[ZAMAN] NTP basarili: %s (unix: %lu)\n", buf, _ntpTime);
  
  // Hemen yedekle
  timeSaveBackup();
  
  return true;
}

// ==========================================
// timeSaveBackup - Zamani LittleFS'e kaydet
// ==========================================
void timeSaveBackup() {
  if (!_timeValid) return;
  
  StaticJsonDocument<128> doc;
  doc["time"] = timeNow();
  doc["saved"] = millis();
  
  String json;
  serializeJson(doc, json);
  
  if (storageWrite(PATH_TIME, json)) {
    _lastSave = millis();
    Serial.println("[ZAMAN] Yedek kaydedildi");
  } else {
    Serial.println("[ZAMAN] Yedek kaydetme hatasi!");
  }
}

// ==========================================
// timeLoadBackup - Yedekten zamani yukle
// ==========================================
void timeLoadBackup() {
  if (!storageExists(PATH_TIME)) {
    Serial.println("[ZAMAN] Yedek dosyasi bulunamadi");
    return;
  }
  
  String json = storageRead(PATH_TIME);
  if (json.length() == 0) {
    Serial.println("[ZAMAN] Yedek dosyasi bos");
    return;
  }
  
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("[ZAMAN] Yedek parse hatasi: %s\n", err.c_str());
    return;
  }
  
  _ntpTime = doc["time"] | 0UL;
  if (_ntpTime == 0) {
    Serial.println("[ZAMAN] Yedekte gecerli zaman yok");
    return;
  }
  
  // ESP32 yeniden baslatildi, millis() sifirdan baslar
  // Kaydedilen zaman yaklasik olarak dogrudur (kaydetme-reboot arasi kayip)
  _ntpMillis = millis();
  _timeValid = true;
  
  char buf[20];
  timeFormat(buf, _ntpTime);
  Serial.printf("[ZAMAN] Yedekten yuklendi: %s (unix: %lu)\n", buf, _ntpTime);
  Serial.println("[ZAMAN] UYARI: Reboot suresi kadar sapma olabilir");
}

// ==========================================
// timeFormat - Unix timestamp -> "HH:MM:SS"
// ==========================================
void timeFormat(char* buf, unsigned long ts) {
  if (ts == 0) {
    strcpy(buf, "--:--:--");
    return;
  }
  time_t t = (time_t)ts;
  struct tm* tm = localtime(&t);
  if (tm) {
    sprintf(buf, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
  } else {
    strcpy(buf, "--:--:--");
  }
}

// ==========================================
// dateFormat - Unix timestamp -> "YYYY-MM-DD"
// ==========================================
void dateFormat(char* buf, unsigned long ts) {
  if (ts == 0) {
    strcpy(buf, "----.--.--");
    return;
  }
  time_t t = (time_t)ts;
  struct tm* tm = localtime(&t);
  if (tm) {
    sprintf(buf, "%04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
  } else {
    strcpy(buf, "----.--.--");
  }
}

// ==========================================
// timeGetHour / timeGetMinute
// ==========================================
uint8_t timeGetHour() {
  unsigned long ts = timeNow();
  if (ts == 0) return 0;
  time_t t = (time_t)ts;
  struct tm* tm = localtime(&t);
  return tm ? tm->tm_hour : 0;
}

uint8_t timeGetMinute() {
  unsigned long ts = timeNow();
  if (ts == 0) return 0;
  time_t t = (time_t)ts;
  struct tm* tm = localtime(&t);
  return tm ? tm->tm_min : 0;
}

// ==========================================
// timeIsValid
// ==========================================
bool timeIsValid() {
  return _timeValid;
}

// ==========================================
// timeTick - Periyodik senkronizasyon ve yedekleme
// ==========================================
void timeTick() {
  unsigned long now = millis();
  
  // millis() overflow tespiti ve duzeltme
  if (now < _prevMillis && _timeValid) {
    Serial.println("[ZAMAN] millis() overflow tespit edildi, referans guncelleniyor");
    _ntpTime = timeNow();
    _ntpMillis = now;
    timeSaveBackup();
  }
  _prevMillis = now;
  
  // WiFi bagliysa periyodik NTP sync
  if (WiFi.status() == WL_CONNECTED) {
    if (now - _lastSyncAttempt > NTP_SYNC_INTERVAL || _lastSyncAttempt == 0) {
      timeSyncNTP();
    }
  }
  
  // Periyodik yedekleme
  if (_timeValid && (now - _lastSave > TIME_SAVE_INTERVAL)) {
    timeSaveBackup();
  }
}

// ==========================================
// timeInit - Baslangic: NTP veya yedekten yukle
// ==========================================
void timeInit() {
  _timeValid = false;
  _lastSyncAttempt = 0;
  _lastSave = 0;
  _prevMillis = millis();
  
  Serial.println("[ZAMAN] Baslatiliyor...");
  
  // Oncelik 1: NTP sync dene
  if (WiFi.status() == WL_CONNECTED) {
    if (timeSyncNTP()) {
      Serial.println("[ZAMAN] NTP ile senkronize edildi");
      return;
    }
  }
  
  // Oncelik 2: Yedekten yukle
  timeLoadBackup();
  
  if (!_timeValid) {
    Serial.println("[ZAMAN] UYARI: Gecerli zaman bulunamadi! WiFi baglantisi gerekli.");
  }
}
