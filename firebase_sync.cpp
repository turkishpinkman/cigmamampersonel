#include "firebase_sync.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "time_manager.h"
#include "employee_manager.h"
#include "event_logger.h"
#include "system_controller.h"
#include "ota_manager.h"
#include <FS.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
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
  if (_authenticated && (long)(_tokenExpiry - millis()) > 0) return true;
  if (_refreshToken.length() > 0) return _refreshAuthToken();
  return _authenticate();
}

// ==========================================
// Firestore REST API yardimci fonksiyonlari
// ==========================================

// Firestore'a dokuman olustur (POST) - Mukerrer kayitlari onlemek icin documentId destekli
static bool _firestoreCreate(const char* collection, const String& fieldsJson, const char* docId = nullptr) {
  if (!_ensureAuth()) return false;

  HTTPClient http;
  String url = _firestoreBase + collection;
  if (docId && strlen(docId) > 0) {
    url += "?documentId=";
    url += docId;
  }
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + _idToken);
  http.setTimeout(10000);

  String body = "{\"fields\":" + fieldsJson + "}";
  int httpCode = http.POST(body);
  http.end();

  // 200/201: Basariyla olusturuldu
  // 409 (ALREADY_EXISTS): Sunucuya daha once ulasmis (ag kopmasi sonrasi tekrar gonderimde mukerrer onleme)
  if (httpCode == 200 || httpCode == 409) return true;
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

// Firestore dokuman guncelle (PATCH) - updateMask destekli kismi guncelleme
static bool _firestorePatch(const char* docPath, const String& fieldsJson, const char* updateMask = nullptr) {
  if (!_ensureAuth()) return false;

  HTTPClient http;
  String url = _firestoreBase + docPath;
  if (updateMask && strlen(updateMask) > 0) {
    url += "?";
    url += updateMask;
  }
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

static uint8_t _parseClamped(const char* str, uint8_t minVal, uint8_t maxVal, uint8_t defaultVal) {
  if (!str || strlen(str) == 0) return defaultVal;
  int v = atoi(str);
  if (v < (int)minVal) return minVal;
  if (v > (int)maxVal) return maxVal;
  return (uint8_t)v;
}

// ==========================================
// Firestore fields JSON olusturma yardimcilari
// ==========================================
static String _strField(const char* val) {
  if (!val) return "{\"stringValue\":\"\"}";
  char esc[96];
  jsonEscape(val, esc, sizeof(esc));
  return String("{\"stringValue\":\"") + esc + "\"}";
}

static String _intField(unsigned long val) {
  return String("{\"integerValue\":\"") + String(val) + "\"}";
}

static String _boolField(bool val) {
  return String("{\"booleanValue\":") + (val ? "true" : "false") + "}";
}

// ==========================================
// syncUploadPendingEvents - Olaylari yukle (WDT ve Batch korumali)
// ==========================================
void syncUploadPendingEvents() {
  if (!wifiIsConnected()) return;

  int count;
  LogEvent* events = eventGetPending(count);
  if (count == 0) return;

  int uploaded = 0;
  int failed = 0;
  bool anyUploaded = false;

  for (int i = 0; i < count; i++) {
    if (events[i].synced) continue;
    if (events[i].uid[0] == '\0') continue;

    // Watchdog sayacini her olay gonderiminde besle
    esp_task_wdt_reset();

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

    // Deterministik dokuman ID: Ag kopmasi durumunda mukerrer kayit olusmasini onler
    char docId[48];
    snprintf(docId, sizeof(docId), "EVT_%s_%lu", FIREBASE_DEVICE_ID, events[i].id);

    if (_firestoreCreate("events", fields, docId)) {
      // Diske her seferinde yazma yapma, RAM'de isaretle
      eventMarkSyncedNoSave(events[i].id);
      uploaded++;
      anyUploaded = true;
    } else {
      failed++;
      if (failed >= 2) {
        Serial.println("[SYNC] Firestore hatasi, yukleme sonraki tura birakildi");
        break;
      }
    }

    yield();

    // SYNC_BATCH_SIZE (5) siniri: Ana loop'un ve dokunmatigin donmasini onler
    if (uploaded >= SYNC_BATCH_SIZE) {
      break;
    }

    delay(50);
  }

  // Batch tamamlandiginda LittleFS'e TEK BIR SEFERDE kaydet (Flash omrunu korur)
  if (anyUploaded) {
    eventSave();
    Serial.printf("[SYNC] %d olay Firestore'a basariyla yuklendi ve kaydedildi\n", uploaded);
  }
}

// ==========================================
// syncDownloadEmployees - Personel listesini indir
// ==========================================
void syncDownloadEmployees() {
  if (!wifiIsConnected()) return;

  String response = _firestoreList("employees", 100);
  if (response.length() == 0) return;

  // JSON boyutunu yanita gore dinamik ayarla (ESP32 bos heap durumuna gore 48KB'a kadar)
  size_t freeHeap = ESP.getFreeHeap();
  size_t docSize = response.length() + 2048;
  if (docSize > 49152) docSize = 49152;
  if (freeHeap > 60000 && docSize < 32768) docSize = 32768;

  DynamicJsonDocument doc(docSize);
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("[SYNC] Personel JSON parse hatasi: %s (Boyut: %u, Heap: %u)\n", err.c_str(), (unsigned int)docSize, (unsigned int)freeHeap);
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
    bool hasCustomShift = fields["hasCustomShift"]["booleanValue"] | false;

    if (strlen(uid) == 0) continue;

    // Eger bu personel yerelde silinmisse ve cevrimdisi kuyrukta bekliyorsa, buluttan geri ekleme!
    if (syncIsEmployeePendingDelete(uid)) {
      Serial.printf("[SYNC] Personel yerelde silinmis ve kuyrukta bekliyor, indirmeden atlandi: %s\n", uid);
      continue;
    }
    ShiftConfig customShift = {8, 0, 17, 0};
    if (hasCustomShift) {
      if (fields.containsKey("shiftStartHour")) {
        const char* val = fields["shiftStartHour"]["integerValue"] | "";
        customShift.startHour = _parseClamped(val, 0, 23, 8);
      }
      if (fields.containsKey("shiftStartMinute")) {
        const char* val = fields["shiftStartMinute"]["integerValue"] | "";
        customShift.startMinute = _parseClamped(val, 0, 59, 0);
      }
      if (fields.containsKey("shiftEndHour")) {
        const char* val = fields["shiftEndHour"]["integerValue"] | "";
        customShift.endHour = _parseClamped(val, 0, 23, 17);
      }
      if (fields.containsKey("shiftEndMinute")) {
        const char* val = fields["shiftEndMinute"]["integerValue"] | "";
        customShift.endMinute = _parseClamped(val, 0, 59, 0);
      }
    }

    if (strlen(uid) == 0) continue;

    int idx = employeeFind(uid);
    if (idx == -1 && active) {
      // Kapasite siniri kontrolu (MAX_EMPLOYEES)
      if (employeeCount() >= MAX_EMPLOYEES) {
        Serial.printf("[SYNC] UYARI: Cihaz maksimum personel kapasitesine (%d) ulasti! %s eklenemedi.\n", MAX_EMPLOYEES, name);
        continue;
      }
      // Yeni personel - yerel listeye ekle
      if (employeeAdd(uid, name)) {
        int newIdx = employeeFind(uid);
        if (newIdx >= 0) {
          Employee* e = employeeGet(newIdx);
          if (e) {
            e->hasCustomShift = hasCustomShift;
            e->customShift = customShift;
            employeeSave();
          }
        }
        Serial.printf("[SYNC] Yeni personel eklendi: %s (%s) [Ozel Mesai: %s]\n",
                      name, uid, hasCustomShift ? "EVET" : "HAYIR");
        synced++;
      }
    } else if (idx >= 0 && !active) {
      // Silinen personel - yerel listeden kaldir
      employeeRemove(idx);
      Serial.printf("[SYNC] Personel silindi: %s\n", uid);
      synced++;
    } else if (idx >= 0) {
      // Mevcut personel - isim veya ozel mesai guncellemesi kontrolu
      Employee* emp = employeeGet(idx);
      if (emp) {
        bool changed = false;
        if (strcmp(emp->name, name) != 0) {
          strncpy(emp->name, name, sizeof(emp->name) - 1);
          emp->name[sizeof(emp->name) - 1] = '\0';
          changed = true;
        }
        if (emp->hasCustomShift != hasCustomShift) {
          emp->hasCustomShift = hasCustomShift;
          changed = true;
        }
        if (hasCustomShift) {
          if (emp->customShift.startHour != customShift.startHour ||
              emp->customShift.startMinute != customShift.startMinute ||
              emp->customShift.endHour != customShift.endHour ||
              emp->customShift.endMinute != customShift.endMinute) {
            emp->customShift = customShift;
            changed = true;
          }
        }
        if (changed) {
          employeeSave();
          synced++;
        }
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

  // Eger yerel mesai ayari henuz buluta iletilemediyse (cevrimdisi degistirildiyse),
  // buluttan gelen eski saatlerin yerel ayari ezmesini onle!
  if (syncIsShiftPendingUpload()) {
    Serial.println(F("[SYNC] Yerel mesai saati kuyrukta bekliyor, bulut ayari atlandi."));
    return;
  }

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
  ShiftConfig currentShift = systemGetShift();
  ShiftConfig newShift = currentShift;
  bool shiftDownloaded = false;

  // Integer degerlerini Firestore formatindan cikart
  // Firestore integerValue string olarak gelir
  if (fields.containsKey("shiftStartHour")) {
    const char* val = fields["shiftStartHour"]["integerValue"] | "";
    newShift.startHour = _parseClamped(val, 0, 23, currentShift.startHour);
    shiftDownloaded = true;
  }
  if (fields.containsKey("shiftStartMinute")) {
    const char* val = fields["shiftStartMinute"]["integerValue"] | "";
    newShift.startMinute = _parseClamped(val, 0, 59, currentShift.startMinute);
    shiftDownloaded = true;
  }
  if (fields.containsKey("shiftEndHour")) {
    const char* val = fields["shiftEndHour"]["integerValue"] | "";
    newShift.endHour = _parseClamped(val, 0, 23, currentShift.endHour);
    shiftDownloaded = true;
  }
  if (fields.containsKey("shiftEndMinute")) {
    const char* val = fields["shiftEndMinute"]["integerValue"] | "";
    newShift.endMinute = _parseClamped(val, 0, 59, currentShift.endMinute);
    shiftDownloaded = true;
  }

  if (shiftDownloaded && (newShift.startHour != currentShift.startHour ||
      newShift.startMinute != currentShift.startMinute ||
      newShift.endHour != currentShift.endHour ||
      newShift.endMinute != currentShift.endMinute)) {
    systemSetShift(newShift);
    Serial.printf("[SYNC] Yeni mesai saatleri sisteme uygulandi ve LittleFS'e kaydedildi: %02d:%02d-%02d:%02d\n",
                  newShift.startHour, newShift.startMinute,
                  newShift.endHour, newShift.endMinute);
  } else {
    Serial.printf("[SYNC] Ayarlar indirildi - Mesai ayni: %02d:%02d-%02d:%02d\n",
                  newShift.startHour, newShift.startMinute,
                  newShift.endHour, newShift.endMinute);
  }

  // Admin kart UID guncelleme
  if (fields.containsKey("adminUID")) {
    const char* aUid = fields["adminUID"]["stringValue"] | "";
    if (strlen(aUid) > 0) {
      systemSetAdminUID(aUid);
    }
  }

  // BULUTTAN FIRMWARE GUNCELLEME KONTROLU (Cloud OTA)
  // SADECE ve SADECE web panelinden yeni bir guncelleme emri verildiginde calisir.
  // Bir kez yuklenen surum cihazin flash'ina (/ota_version.txt) kaydedilir ve
  // ayni surum veya eski surum icin ASLA tekrar indirme yapilmaz.
  if (fields.containsKey("firmwareTargetVersion") && fields.containsKey("firmwareUrl")) {
    int targetVer = 0;
    const char* verStr = fields["firmwareTargetVersion"]["integerValue"] | "";
    if (strlen(verStr) > 0) targetVer = atoi(verStr);
    const char* binUrl = fields["firmwareUrl"]["stringValue"] | "";

    // Cihazin daha once bu surumu yukleyip yuklemedigini flash'tan kontrol et
    int installedVersion = CURRENT_FIRMWARE_VERSION;
    if (storageExists("/ota_version.txt")) {
      String savedVer = storageRead("/ota_version.txt");
      int sVal = savedVer.toInt();
      if (sVal > installedVersion) installedVersion = sVal;
    }

    // Sadece hedef surum cihazin yuklu surumunden kesin olarak buyukse ve URL gecerliyse
    if (targetVer > installedVersion && strlen(binUrl) > 10) {
      Serial.printf("[Cloud OTA] Web panelinden yeni guncelleme emri alindi! Hedef: %d (Mevcut: %d)\n",
                    targetVer, installedVersion);
      Serial.printf("[Cloud OTA] Indirme adresi: %s\n", binUrl);

      // Indirme ve flashlama islemini baslat (surum numarasini flash'a isleyip yeniden baslatir)
      otaPerformCloudUpdate(binUrl, targetVer);
    }
  }
}

// ==========================================
// OUTBOX (CEVRIMDISI MUTASYON KUYRUGU) YONETIMI
// ==========================================

static void _queueOutboxLine(const String& line) {
  storageAppend(PATH_OUTBOX, line);
}

void syncQueueEmployeeDelete(const char* uid) {
  if (!uid || strlen(uid) == 0) return;
  char cleanUid[32];
  normalizeUID(uid, cleanUid, sizeof(cleanUid));
  String line = "{\"op\":\"DEL\",\"uid\":\"" + String(cleanUid) + "\"}\n";
  _queueOutboxLine(line);
  Serial.printf("[OUTBOX] Personel silme cevrimdisi kuyruga alindi: %s\n", cleanUid);
}

void syncQueueEmployeeAdd(const char* uid, const char* name) {
  if (!uid || !name) return;
  char escName[64];
  jsonEscape(name, escName, sizeof(escName));
  char cleanUid[32];
  normalizeUID(uid, cleanUid, sizeof(cleanUid));
  String line = "{\"op\":\"ADD\",\"uid\":\"" + String(cleanUid) + "\",\"name\":\"" + String(escName) + "\"}\n";
  _queueOutboxLine(line);
  Serial.printf("[OUTBOX] Personel ekleme cevrimdisi kuyruga alindi: %s (%s)\n", escName, cleanUid);
}

void syncQueueShiftUpdate(const ShiftConfig& shift) {
  String line = "{\"op\":\"SHIFT\",\"sh\":" + String(shift.startHour) +
                ",\"sm\":" + String(shift.startMinute) +
                ",\"eh\":" + String(shift.endHour) +
                ",\"em\":" + String(shift.endMinute) + "}\n";
  _queueOutboxLine(line);
  Serial.println(F("[OUTBOX] Mesai saati guncellemesi cevrimdisi kuyruga alindi"));
}

bool syncIsEmployeePendingDelete(const char* uid) {
  if (!uid || !storageExists(PATH_OUTBOX)) return false;
  char cleanUid[32];
  normalizeUID(uid, cleanUid, sizeof(cleanUid));
  String outbox = storageRead(PATH_OUTBOX);
  return (outbox.indexOf("\"op\":\"DEL\"") >= 0 && outbox.indexOf(cleanUid) >= 0);
}

bool syncIsShiftPendingUpload() {
  if (!storageExists(PATH_OUTBOX)) return false;
  String outbox = storageRead(PATH_OUTBOX);
  return (outbox.indexOf("\"op\":\"SHIFT\"") >= 0);
}

// ==========================================
// syncDeleteEmployee - Personeli Firestore'da pasife al
// ==========================================
bool syncDeleteEmployee(const char* uid) {
  if (!uid || strlen(uid) == 0) return false;

  String docPath = "employees/";
  char cleanUid[32];
  normalizeUID(uid, cleanUid, sizeof(cleanUid));
  docPath += cleanUid;

  String fields = "{";
  fields += "\"active\":{\"booleanValue\":false},";
  fields += "\"state\":{\"integerValue\":\"0\"},";
  fields += "\"lastUpdate\":" + _intField(timeNow());
  fields += "}";

  bool ok = false;
  if (wifiIsConnected() && _ensureAuth()) {
    ok = _firestorePatch(docPath.c_str(), fields, "updateMask.fieldPaths=active&updateMask.fieldPaths=state&updateMask.fieldPaths=lastUpdate");
  }

  if (ok) {
    Serial.printf("[SYNC] Personel Firestore'da pasife alindi: %s\n", uid);
  } else {
    Serial.printf("[SYNC] Personel silme cevrimdisi kuyruga alindi: %s\n", uid);
    syncQueueEmployeeDelete(uid);
  }
  return ok;
}

// ==========================================
// syncUploadShiftSettings - Cihazdan mesai saatlerini Firestore'a yukle
// ==========================================
bool syncUploadShiftSettings(const ShiftConfig& shift) {
  String docPath = "settings/main";
  String fields = "{";
  fields += "\"shiftStartHour\":" + _intField(shift.startHour) + ",";
  fields += "\"shiftStartMinute\":" + _intField(shift.startMinute) + ",";
  fields += "\"shiftEndHour\":" + _intField(shift.endHour) + ",";
  fields += "\"shiftEndMinute\":" + _intField(shift.endMinute) + ",";
  fields += "\"updatedAt\":" + _intField(timeNow());
  fields += "}";

  bool ok = false;
  if (wifiIsConnected() && _ensureAuth()) {
    ok = _firestorePatch(docPath.c_str(), fields, "updateMask.fieldPaths=shiftStartHour&updateMask.fieldPaths=shiftStartMinute&updateMask.fieldPaths=shiftEndHour&updateMask.fieldPaths=shiftEndMinute&updateMask.fieldPaths=updatedAt");
  }

  if (ok) {
    Serial.printf("[SYNC] Cihazdan yeni mesai saatleri Firestore'a aktarildi: %02d:%02d - %02d:%02d\n",
                  shift.startHour, shift.startMinute, shift.endHour, shift.endMinute);
  } else {
    Serial.println(F("[SYNC] Mesai saatleri guncellemesi cevrimdisi kuyruga alindi."));
    syncQueueShiftUpdate(shift);
  }
  return ok;
}

// ==========================================
// syncUploadNewEmployee - Yeni personeli yukle (Mevcut durumu korur ve updateMask kullanir)
// ==========================================
bool syncUploadNewEmployee(const char* uid, const char* name) {
  if (!uid || !name) return false;

  String docPath = "employees/";
  String safeUid = uid;
  safeUid.replace(":", "");
  docPath += safeUid;

  int idx = employeeFind(uid);
  Employee* emp = (idx >= 0) ? employeeGet(idx) : nullptr;
  bool hasCustom = (emp && emp->hasCustomShift);
  unsigned long currentState = emp ? (unsigned long)emp->state : 0;

  String fields = "{";
  fields += "\"uid\":" + _strField(uid) + ",";
  fields += "\"name\":" + _strField(name) + ",";
  fields += "\"state\":" + _intField(currentState) + ",";
  fields += "\"active\":" + _boolField(true) + ",";
  fields += "\"hasCustomShift\":" + _boolField(hasCustom) + ",";
  if (hasCustom && emp) {
    fields += "\"shiftStartHour\":" + _intField(emp->customShift.startHour) + ",";
    fields += "\"shiftStartMinute\":" + _intField(emp->customShift.startMinute) + ",";
    fields += "\"shiftEndHour\":" + _intField(emp->customShift.endHour) + ",";
    fields += "\"shiftEndMinute\":" + _intField(emp->customShift.endMinute) + ",";
  }
  fields += "\"lastUpdate\":" + _intField(timeNow()) + ",";
  fields += "\"createdAt\":" + _intField(timeNow());
  fields += "}";

  String mask = "updateMask.fieldPaths=uid&updateMask.fieldPaths=name&updateMask.fieldPaths=state&updateMask.fieldPaths=active&updateMask.fieldPaths=hasCustomShift&updateMask.fieldPaths=lastUpdate&updateMask.fieldPaths=createdAt";
  if (hasCustom) {
    mask += "&updateMask.fieldPaths=shiftStartHour&updateMask.fieldPaths=shiftStartMinute&updateMask.fieldPaths=shiftEndHour&updateMask.fieldPaths=shiftEndMinute";
  }

  bool ok = false;
  if (wifiIsConnected() && _ensureAuth()) {
    ok = _firestorePatch(docPath.c_str(), fields, mask.c_str());
  }

  if (ok) {
    Serial.printf("[SYNC] Personel Firestore'a yuklendi: %s (%s) [Durum: %lu]\n", name, uid, currentState);
  } else {
    Serial.printf("[SYNC] Personel cevrimdisi kuyruga alindi: %s (%s)\n", name, uid);
    syncQueueEmployeeAdd(uid, name);
  }
  return ok;
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
// syncProcessOutbox - Cevrimdisi bekleyen mutasyonlari isle
// ==========================================
void syncProcessOutbox() {
  if (!wifiIsConnected() || !_ensureAuth() || !storageExists(PATH_OUTBOX)) return;

  File f = LittleFS.open(PATH_OUTBOX, "r");
  if (!f) return;

  String remaining = "";
  StaticJsonDocument<512> itemDoc;
  bool anyProcessed = false;

  while (f.available()) {
    esp_task_wdt_reset();
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    itemDoc.clear();
    DeserializationError err = deserializeJson(itemDoc, line);
    if (err) continue;

    const char* op = itemDoc["op"] | "";
    bool success = false;

    if (strcmp(op, "DEL") == 0) {
      const char* uid = itemDoc["uid"] | "";
      if (strlen(uid) > 0) {
        String docPath = "employees/" + String(uid);
        String fields = "{\"active\":{\"booleanValue\":false},\"state\":{\"integerValue\":\"0\"},\"lastUpdate\":" + _intField(timeNow()) + "}";
        success = _firestorePatch(docPath.c_str(), fields, "updateMask.fieldPaths=active&updateMask.fieldPaths=state&updateMask.fieldPaths=lastUpdate");
        if (success) {
          Serial.printf("[OUTBOX] Bekleyen silme Firestore'a iletildi: %s\n", uid);
        }
      }
    } else if (strcmp(op, "ADD") == 0) {
      const char* uid = itemDoc["uid"] | "";
      const char* name = itemDoc["name"] | "";
      if (strlen(uid) > 0) {
        // Dogrudan Firestore patch dene
        int idx = employeeFind(uid);
        Employee* emp = (idx >= 0) ? employeeGet(idx) : nullptr;
        bool hasCustom = (emp && emp->hasCustomShift);
        unsigned long currentState = emp ? (unsigned long)emp->state : 0;

        String docPath = "employees/" + String(uid);
        String fields = "{";
        fields += "\"uid\":" + _strField(uid) + ",";
        fields += "\"name\":" + _strField(name) + ",";
        fields += "\"state\":" + _intField(currentState) + ",";
        fields += "\"active\":" + _boolField(true) + ",";
        fields += "\"hasCustomShift\":" + _boolField(hasCustom) + ",";
        if (hasCustom && emp) {
          fields += "\"shiftStartHour\":" + _intField(emp->customShift.startHour) + ",";
          fields += "\"shiftStartMinute\":" + _intField(emp->customShift.startMinute) + ",";
          fields += "\"shiftEndHour\":" + _intField(emp->customShift.endHour) + ",";
          fields += "\"shiftEndMinute\":" + _intField(emp->customShift.endMinute) + ",";
        }
        fields += "\"lastUpdate\":" + _intField(timeNow()) + ",";
        fields += "\"createdAt\":" + _intField(timeNow());
        fields += "}";

        String mask = "updateMask.fieldPaths=uid&updateMask.fieldPaths=name&updateMask.fieldPaths=state&updateMask.fieldPaths=active&updateMask.fieldPaths=hasCustomShift&updateMask.fieldPaths=lastUpdate&updateMask.fieldPaths=createdAt";
        if (hasCustom) {
          mask += "&updateMask.fieldPaths=shiftStartHour&updateMask.fieldPaths=shiftStartMinute&updateMask.fieldPaths=shiftEndHour&updateMask.fieldPaths=shiftEndMinute";
        }
        success = _firestorePatch(docPath.c_str(), fields, mask.c_str());
        if (success) {
          Serial.printf("[OUTBOX] Bekleyen personel ekleme Firestore'a iletildi: %s\n", name);
        }
      }
    } else if (strcmp(op, "SHIFT") == 0) {
      ShiftConfig sc;
      sc.startHour = itemDoc["sh"] | 8;
      sc.startMinute = itemDoc["sm"] | 0;
      sc.endHour = itemDoc["eh"] | 17;
      sc.endMinute = itemDoc["em"] | 0;
      String docPath = "settings/main";
      String fields = "{";
      fields += "\"shiftStartHour\":" + _intField(sc.startHour) + ",";
      fields += "\"shiftStartMinute\":" + _intField(sc.startMinute) + ",";
      fields += "\"shiftEndHour\":" + _intField(sc.endHour) + ",";
      fields += "\"shiftEndMinute\":" + _intField(sc.endMinute) + ",";
      fields += "\"updatedAt\":" + _intField(timeNow());
      fields += "}";
      success = _firestorePatch(docPath.c_str(), fields, "updateMask.fieldPaths=shiftStartHour&updateMask.fieldPaths=shiftStartMinute&updateMask.fieldPaths=shiftEndHour&updateMask.fieldPaths=shiftEndMinute&updateMask.fieldPaths=updatedAt");
      if (success) {
        Serial.println(F("[OUTBOX] Bekleyen mesai saati Firestore'a iletildi"));
      }
    }

    if (success) {
      anyProcessed = true;
    } else {
      remaining += line + "\n";
    }
    yield();
  }
  f.close();

  if (anyProcessed) {
    if (remaining.length() > 0) {
      storageWrite(PATH_OUTBOX, remaining);
    } else {
      storageDelete(PATH_OUTBOX);
      Serial.println(F("[OUTBOX] Tum bekleyen mutasyonlar basariyla Firestore'a esitlendi."));
    }
  }
}

// ==========================================
// syncUploadArchivedEvents - events_archive.json dosyasini buluta gonder
// ==========================================
void syncUploadArchivedEvents() {
  if (!wifiIsConnected() || !_ensureAuth() || !storageExists(PATH_ARCHIVE)) return;

  File f = LittleFS.open(PATH_ARCHIVE, "r");
  if (!f) return;

  StaticJsonDocument<384> itemDoc;
  int uploaded = 0;
  bool failed = false;

  while (f.available()) {
    esp_task_wdt_reset();
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    itemDoc.clear();
    DeserializationError err = deserializeJson(itemDoc, line);
    if (err) continue;

    unsigned long id = itemDoc["id"] | 0UL;
    const char* uid = itemDoc["uid"] | "";
    const char* name = itemDoc["name"] | "";
    int type = itemDoc["type"] | 0;
    unsigned long ts = itemDoc["ts"] | 0UL;
    const char* date = itemDoc["date"] | "";

    String fields = "{";
    fields += "\"uid\":" + _strField(uid) + ",";
    fields += "\"employeeName\":" + _strField(name) + ",";
    fields += "\"type\":" + _strField(eventTypeToStr((EventType)type)) + ",";
    fields += "\"timestamp\":" + _intField(ts) + ",";
    fields += "\"date\":" + _strField(date) + ",";
    fields += "\"deviceId\":" + _strField(FIREBASE_DEVICE_ID) + ",";
    fields += "\"localId\":" + _intField(id);
    fields += "}";

    char docId[48];
    snprintf(docId, sizeof(docId), "EVT_%s_ARCH_%lu", FIREBASE_DEVICE_ID, id);

    if (_firestoreCreate("events", fields, docId)) {
      uploaded++;
    } else {
      failed = true;
      break;
    }
    yield();
  }
  f.close();

  if (uploaded > 0) {
    Serial.printf("[SYNC] %d arsivlenmis olay Firestore'a basariyla aktarildi.\n", uploaded);
  }

  // Tum kayitlar sorunsuz gonderildiyse arsiv dosyasini sil
  if (!failed) {
    storageDelete(PATH_ARCHIVE);
    Serial.println(F("[SYNC] events_archive.json basariyla temizlendi."));
  }
}

// ==========================================
// syncUploadEmployeeState - Durum guncelle
// ==========================================
void syncUploadEmployeeState(const char* uid, EmployeeState state) {
  if (!wifiIsConnected() || !_ensureAuth()) return;

  String docPath = "employees/";
  String safeUid = uid;
  safeUid.replace(":", "");
  docPath += safeUid;

  String fields = "{";
  fields += "\"uid\":" + _strField(uid) + ",";
  fields += "\"state\":" + _intField((unsigned long)state) + ",";
  fields += "\"lastUpdate\":" + _intField(timeNow());
  fields += "}";

  _firestorePatch(docPath.c_str(), fields, "updateMask.fieldPaths=uid&updateMask.fieldPaths=state&updateMask.fieldPaths=lastUpdate");
}

// ==========================================
// syncForceUpload - Hemen yukle
// ==========================================
void syncForceUpload() {
  syncUploadPendingEvents();
  syncUploadArchivedEvents();
  syncProcessOutbox();
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

  // 0. Cevrimdisi bekleyen mutasyonlari ve arsiv olaylarini gonder
  syncProcessOutbox();
  syncUploadArchivedEvents();

  // 1. Olay yukleme (15 saniyede bir)
  if (now - _lastEventSync > SYNC_EVENT_INTERVAL || _lastEventSync == 0) {
    _lastEventSync = now;
    syncUploadPendingEvents();
  }

  // 2. Personel listesi indirme (5 dakikada bir)
  if (now - _lastEmployeeSync > SYNC_EMPLOYEE_INTERVAL || _lastEmployeeSync == 0) {
    _lastEmployeeSync = now;
    syncDownloadEmployees();    // Firestore'daki degisiklikleri cek
  }

  // 3. Ayar indirme (10 dakikada bir)
  if (now - _lastSettingsSync > SYNC_SETTINGS_INTERVAL || _lastSettingsSync == 0) {
    _lastSettingsSync = now;
    syncDownloadSettings();
  }
}
