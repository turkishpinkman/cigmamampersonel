#include "firebase_sync.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "time_manager.h"
#include "employee_manager.h"
#include "event_logger.h"
#include "system_controller.h"
#include "ota_manager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ==========================================
// Dahili degiskenler
// ==========================================
static String _idToken = "";
static String _refreshToken = "";
static unsigned long _tokenExpiry = 0;
static bool _authenticated = false;

static unsigned long _lastEventSync = 0;
static unsigned long _lastEmployeeSync = 0;
static unsigned long _lastSettingsSync = 0;

// Firebase URL'leri
static String _authUrl;
static String _refreshUrl;
static String _firestoreBase;

// ==========================================
// syncInit - Firebase URL'lerini hazirla
// ==========================================
void syncInit() {
  _authUrl = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=";
  _authUrl += FIREBASE_API_KEY;

  _refreshUrl = "https://securetoken.googleapis.com/v1/token?key=";
  _refreshUrl += FIREBASE_API_KEY;

  _firestoreBase = "https://firestore.googleapis.com/v1/projects/";
  _firestoreBase += FIREBASE_PROJECT_ID;
  _firestoreBase += "/databases/(default)/documents/";

  _authenticated = false;
  _lastEventSync = 0;
  _lastEmployeeSync = 0;
  _lastSettingsSync = 0;

  Serial.println("[SYNC] Firebase senkronizasyon modulu hazir");
  Serial.printf("[SYNC] Proje: %s\n", FIREBASE_PROJECT_ID);
}

// ==========================================
// _authenticate - Email/sifre ile giris yap
// ==========================================
static bool _authenticate() {
  if (!wifiIsConnected()) return false;

  Serial.println("[SYNC] Firebase kimlik dogrulama...");

  HTTPClient http;
  http.begin(_authUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  StaticJsonDocument<256> reqDoc;
  reqDoc["email"] = FIREBASE_DEVICE_EMAIL;
  reqDoc["password"] = FIREBASE_DEVICE_PASS;
  reqDoc["returnSecureToken"] = true;

  String body;
  serializeJson(reqDoc, body);

  int httpCode = http.POST(body);

  if (httpCode == 200) {
    String response = http.getString();
    http.end();

    DynamicJsonDocument respDoc(2048);
    DeserializationError err = deserializeJson(respDoc, response);
    if (err) {
      Serial.printf("[SYNC] Auth JSON parse hatasi: %s\n", err.c_str());
      return false;
    }

    _idToken = respDoc["idToken"].as<String>();
    _refreshToken = respDoc["refreshToken"].as<String>();
    int expiresIn = atoi(respDoc["expiresIn"] | "3600");
    // Token'i 5 dakika erken yenile
    _tokenExpiry = millis() + ((unsigned long)expiresIn * 1000UL) - 300000UL;
    _authenticated = true;

    Serial.println("[SYNC] Kimlik dogrulama basarili!");
    return true;
  } else {
    String errBody = http.getString();
    http.end();
    Serial.printf("[SYNC] Kimlik dogrulama hatasi: HTTP %d\n", httpCode);
    Serial.println(errBody.substring(0, 200));
    return false;
  }
}

// ==========================================
// _refreshAuthToken - Token yenileme
// ==========================================
static bool _refreshAuthToken() {
  if (_refreshToken.length() == 0) return _authenticate();

  Serial.println("[SYNC] Token yenileniyor...");

  HTTPClient http;
  http.begin(_refreshUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(10000);

  String body = "grant_type=refresh_token&refresh_token=" + _refreshToken;
  int httpCode = http.POST(body);

  if (httpCode == 200) {
    String response = http.getString();
    http.end();

    DynamicJsonDocument doc(2048);
    deserializeJson(doc, response);

    _idToken = doc["id_token"].as<String>();
    _refreshToken = doc["refresh_token"].as<String>();
    int expiresIn = atoi(doc["expires_in"] | "3600");
    _tokenExpiry = millis() + ((unsigned long)expiresIn * 1000UL) - 300000UL;
    _authenticated = true;

    Serial.println("[SYNC] Token yenileme basarili");
    return true;
  } else {
    http.end();
    Serial.printf("[SYNC] Token yenileme hatasi: %d, yeniden giris deneniyor\n", httpCode);
    return _authenticate();
  }
}

// ==========================================
// _ensureAuth - Token gecerliligi kontrolu
// ==========================================
static bool _ensureAuth() {
  if (!wifiIsConnected()) return false;
  if (_authenticated && millis() < _tokenExpiry) return true;
  if (_refreshToken.length() > 0) return _refreshAuthToken();
  return _authenticate();
}

// ==========================================
// Firestore REST API yardimci fonksiyonlari
// ==========================================

// Firestore'a dokuman olustur (POST)
static bool _firestoreCreate(const char* collection, const String& fieldsJson) {
  if (!_ensureAuth()) return false;

  HTTPClient http;
  String url = _firestoreBase + collection;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + _idToken);
  http.setTimeout(10000);

  String body = "{\"fields\":" + fieldsJson + "}";
  int httpCode = http.POST(body);
  http.end();

  if (httpCode == 200) return true;
  Serial.printf("[SYNC] Firestore POST hatasi: %d (%s)\n", httpCode, collection);
  if (httpCode == 401) _authenticated = false;  // Token gecersiz
  return false;
}

// Firestore'dan koleksiyon oku (GET)
static String _firestoreList(const char* collection, int pageSize = 100) {
  if (!_ensureAuth()) return "";

  HTTPClient http;
  String url = _firestoreBase + collection + "?pageSize=" + String(pageSize);
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + _idToken);
  http.setTimeout(10000);

  int httpCode = http.GET();
  String result = "";

  if (httpCode == 200) {
    result = http.getString();
  } else {
    Serial.printf("[SYNC] Firestore GET hatasi: %d (%s)\n", httpCode, collection);
    if (httpCode == 401) _authenticated = false;
  }

  http.end();
  return result;
}

// Firestore'dan tek dokuman oku
static String _firestoreGet(const char* docPath) {
  if (!_ensureAuth()) return "";

  HTTPClient http;
  String url = _firestoreBase + docPath;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + _idToken);
  http.setTimeout(10000);

  int httpCode = http.GET();
  String result = "";

  if (httpCode == 200) {
    result = http.getString();
  } else {
    Serial.printf("[SYNC] Firestore GET doc hatasi: %d (%s)\n", httpCode, docPath);
    if (httpCode == 401) _authenticated = false;
  }

  http.end();
  return result;
}

// Firestore dokuman guncelle (PATCH)
static bool _firestorePatch(const char* docPath, const String& fieldsJson) {
  if (!_ensureAuth()) return false;

  HTTPClient http;
  String url = _firestoreBase + docPath;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + _idToken);
  http.setTimeout(10000);

  String body = "{\"fields\":" + fieldsJson + "}";
  int httpCode = http.PATCH(body);
  http.end();

  if (httpCode == 200) return true;
  Serial.printf("[SYNC] Firestore PATCH hatasi: %d (%s)\n", httpCode, docPath);
  if (httpCode == 401) _authenticated = false;
  return false;
}

// ==========================================
// Firestore fields JSON olusturma yardimcilari
// ==========================================
static String _strField(const char* val) {
  return String("{\"stringValue\":\"") + val + "\"}";
}

static String _intField(unsigned long val) {
  return String("{\"integerValue\":\"") + String(val) + "\"}";
}

static String _boolField(bool val) {
  return String("{\"booleanValue\":") + (val ? "true" : "false") + "}";
}

// ==========================================
// syncUploadPendingEvents - Olaylari yukle
// ==========================================
void syncUploadPendingEvents() {
  if (!wifiIsConnected()) return;

  int count;
  LogEvent* events = eventGetPending(count);
  if (count == 0) return;

  int uploaded = 0;
  int failed = 0;

  for (int i = 0; i < count; i++) {
    if (events[i].synced) continue;
    if (events[i].uid[0] == '\0') continue;

    // Olay icin Firestore fields JSON olustur
    String fields = "{";
    fields += "\"uid\":" + _strField(events[i].uid) + ",";
    fields += "\"employeeName\":" + _strField(events[i].name) + ",";
    fields += "\"type\":" + _strField(eventTypeToStr(events[i].type)) + ",";
    fields += "\"timestamp\":" + _intField(events[i].timestamp) + ",";
    fields += "\"date\":" + _strField(events[i].date) + ",";
    fields += "\"deviceId\":" + _strField(FIREBASE_DEVICE_ID) + ",";
    fields += "\"localId\":" + _intField(events[i].id);
    fields += "}";

    if (_firestoreCreate("events", fields)) {
      eventMarkSynced(events[i].id);
      uploaded++;
    } else {
      failed++;
      if (failed >= 3) {
        Serial.println("[SYNC] 3 ardisik hata, yukleme durduruldu");
        break;
      }
    }

    delay(100);  // Firebase rate limit koruması
  }

  if (uploaded > 0) {
    Serial.printf("[SYNC] %d olay Firestore'a yuklendi\n", uploaded);
  }
}

// ==========================================
// syncDownloadEmployees - Personel listesini indir
// ==========================================
void syncDownloadEmployees() {
  if (!wifiIsConnected()) return;

  String response = _firestoreList("employees", 100);
  if (response.length() == 0) return;

  // JSON boyutunu yanita gore ayarla
  size_t docSize = response.length() + 1024;
  if (docSize > 16384) docSize = 16384;

  DynamicJsonDocument doc(docSize);
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("[SYNC] Personel JSON parse hatasi: %s\n", err.c_str());
    return;
  }

  JsonArray documents = doc["documents"];
  if (documents.isNull()) {
    Serial.println("[SYNC] Firestore'da personel bulunamadi");
    return;
  }

  int synced = 0;
  for (JsonObject docObj : documents) {
    JsonObject fields = docObj["fields"];
    if (fields.isNull()) continue;

    const char* uid = fields["uid"]["stringValue"] | "";
    const char* name = fields["name"]["stringValue"] | "";
    bool active = fields["active"]["booleanValue"] | true;

    if (strlen(uid) == 0) continue;

    int idx = employeeFind(uid);
    if (idx == -1 && active) {
      // Yeni personel - yerel listeye ekle
      if (employeeAdd(uid, name)) {
        Serial.printf("[SYNC] Yeni personel eklendi: %s (%s)\n", name, uid);
        synced++;
      }
    } else if (idx >= 0 && !active) {
      // Silinen personel - yerel listeden kaldir
      employeeRemove(idx);
      Serial.printf("[SYNC] Personel silindi: %s\n", uid);
      synced++;
    } else if (idx >= 0) {
      // Mevcut personel - isim guncellemesi kontrolu
      Employee* emp = employeeGet(idx);
      if (emp && strcmp(emp->name, name) != 0) {
        strncpy(emp->name, name, sizeof(emp->name) - 1);
        employeeSave();
        synced++;
      }
    }
  }

  if (synced > 0) {
    Serial.printf("[SYNC] %d personel senkronize edildi\n", synced);
  }
}

// ==========================================
// syncDownloadSettings - Ayarlari indir
// ==========================================
void syncDownloadSettings() {
  if (!wifiIsConnected()) return;

  String response = _firestoreGet("settings/main");
  if (response.length() == 0) return;

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("[SYNC] Ayar JSON parse hatasi: %s\n", err.c_str());
    return;
  }

  JsonObject fields = doc["fields"];
  if (fields.isNull()) return;

  // Mesai saatlerini guncelle
  bool changed = false;
  ShiftConfig newShift;

  // Integer degerlerini Firestore formatindan cikart
  // Firestore integerValue string olarak gelir
  if (fields.containsKey("shiftStartHour")) {
    const char* val = fields["shiftStartHour"]["integerValue"] | "";
    if (strlen(val) > 0) newShift.startHour = atoi(val);
  }
  if (fields.containsKey("shiftStartMinute")) {
    const char* val = fields["shiftStartMinute"]["integerValue"] | "";
    if (strlen(val) > 0) newShift.startMinute = atoi(val);
  }
  if (fields.containsKey("shiftEndHour")) {
    const char* val = fields["shiftEndHour"]["integerValue"] | "";
    if (strlen(val) > 0) newShift.endHour = atoi(val);
  }
  if (fields.containsKey("shiftEndMinute")) {
    const char* val = fields["shiftEndMinute"]["integerValue"] | "";
    if (strlen(val) > 0) newShift.endMinute = atoi(val);
  }

  Serial.printf("[SYNC] Ayarlar indirildi - Mesai: %02d:%02d-%02d:%02d\n",
                newShift.startHour, newShift.startMinute,
                newShift.endHour, newShift.endMinute);

  // BULUTTAN FIRMWARE GUNCELLEME KONTROLU (Cloud OTA)
  if (fields.containsKey("firmwareTargetVersion") && fields.containsKey("firmwareUrl")) {
    int targetVer = 0;
    const char* verStr = fields["firmwareTargetVersion"]["integerValue"] | "";
    if (strlen(verStr) > 0) targetVer = atoi(verStr);
    const char* binUrl = fields["firmwareUrl"]["stringValue"] | "";

    if (targetVer > CURRENT_FIRMWARE_VERSION && strlen(binUrl) > 10) {
      Serial.printf("[Cloud OTA] Yeni firmware tespit edildi! Hedef surum: %d (Mevcut: %d)\n",
                    targetVer, CURRENT_FIRMWARE_VERSION);
      Serial.printf("[Cloud OTA] Indirme adresi: %s\n", binUrl);
      otaPerformCloudUpdate(binUrl);
    }
  }
}

// ==========================================
// syncUploadNewEmployee - Yeni personeli yukle
// ==========================================
void syncUploadNewEmployee(const char* uid, const char* name) {
  if (!wifiIsConnected() || !_ensureAuth()) return;

  String docPath = "employees/";
  String safeUid = uid;
  safeUid.replace(":", "");
  docPath += safeUid;

  String fields = "{";
  fields += "\"uid\":" + _strField(uid) + ",";
  fields += "\"name\":" + _strField(name) + ",";
  fields += "\"state\":" + _intField(0) + ",";
  fields += "\"active\":" + _boolField(true) + ",";
  fields += "\"lastUpdate\":" + _intField(timeNow()) + ",";
  fields += "\"createdAt\":" + _intField(timeNow());
  fields += "}";

  if (_firestorePatch(docPath.c_str(), fields)) {
    Serial.printf("[SYNC] Yeni personel Firestore'a yuklendi: %s (%s)\n", name, uid);
  } else {
    Serial.printf("[SYNC] Personel yukleme basarisiz: %s\n", uid);
  }
}

// ==========================================
// syncUploadAllEmployees - Tum personelleri yukle
// ==========================================
void syncUploadAllEmployees() {
  if (!wifiIsConnected() || !_ensureAuth()) return;

  int total = employeeGetTotalSlots();
  Employee* emps = employeeGetAll();
  if (!emps) return;

  int count = 0;
  for (int i = 0; i < total; i++) {
    if (emps[i].uid[0] != '\0' && emps[i].active) {
      syncUploadNewEmployee(emps[i].uid, emps[i].name);
      count++;
      delay(150);
    }
  }
  if (count > 0) {
    Serial.printf("[SYNC] Toplam %d personel Firestore'a esitlendi.\n", count);
  }
}

// ==========================================
// syncUploadEmployeeState - Durum guncelle
// ==========================================
void syncUploadEmployeeState(const char* uid, EmployeeState state) {
  if (!wifiIsConnected() || !_ensureAuth()) return;

  // Firestore'da UID ile eslesen dokumani bul ve guncelle
  // Basit yaklasim: employees/{uid} olarak kaydet
  String docPath = "employees/";
  // UID'deki ':' karakterlerini kaldir (Firestore doc ID icin)
  String safeUid = uid;
  safeUid.replace(":", "");
  docPath += safeUid;

  String fields = "{";
  fields += "\"uid\":" + _strField(uid) + ",";
  fields += "\"state\":" + _intField((unsigned long)state) + ",";
  fields += "\"lastUpdate\":" + _intField(timeNow());
  fields += "}";

  _firestorePatch(docPath.c_str(), fields);
}

// ==========================================
// syncForceUpload - Hemen yukle
// ==========================================
void syncForceUpload() {
  syncUploadPendingEvents();
}

// ==========================================
// syncIsConnected
// ==========================================
bool syncIsConnected() {
  return _authenticated && wifiIsConnected();
}

// ==========================================
// syncTick - Periyodik senkronizasyon
// ==========================================
void syncTick() {
  if (!wifiIsConnected()) return;

  unsigned long now = millis();

  // Olay yukleme (30 saniyede bir)
  if (now - _lastEventSync > SYNC_EVENT_INTERVAL || _lastEventSync == 0) {
    _lastEventSync = now;
    syncUploadPendingEvents();
  }

  // Personel listesi indirme (5 dakikada bir)
  if (now - _lastEmployeeSync > SYNC_EMPLOYEE_INTERVAL || _lastEmployeeSync == 0) {
    _lastEmployeeSync = now;
    syncUploadAllEmployees();   // Once cihazdaki kayitli personelleri Firestore'a gonder
    syncDownloadEmployees();    // Sonra Firestore'daki degisiklikleri cek
  }

  // Ayar indirme (10 dakikada bir)
  if (now - _lastSettingsSync > SYNC_SETTINGS_INTERVAL || _lastSettingsSync == 0) {
    _lastSettingsSync = now;
    syncDownloadSettings();
  }
}
