#include "config.h"
#include "system_controller.h"
#include "buzzer.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "time_manager.h"
#include "nfc_reader.h"
#include "display_manager.h"
#include "touch_ui.h"
#include "employee_manager.h"
#include "event_logger.h"
#include "firebase_sync.h"
#include <ArduinoJson.h>

// =============================================================================
// CIGMAMAM PERSONEL SISTEMI - SISTEM KONTROLCUSU IMPLEMENTASYONU
// =============================================================================

// Eger config.h icerisinde tanimlanmamis ise guvenli varsayilanlar
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID "Cigmamam_WiFi"
#endif

#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS "12345678"
#endif

#ifndef COLOR_LIGHT_BLUE
#define COLOR_LIGHT_BLUE 0x7D7C
#endif

// Dahili durum degiskenleri
static ScreenState _currentScreen = SCR_MAIN;
static int _selectedEmployee = -1;          // Secim ekranindaki personel indeksi
static char _lastCardUID[24] = "";          // Son okunan kart UID
static unsigned long _lastCardTime = 0;     // Son kart okuma zamani (debounce)
static unsigned long _screenTimer = 0;      // Ekran timeout zamanlayici
static unsigned long _lastMainRefresh = 0;  // Ana ekran son yenileme zamani
static bool _processing = false;            // Islem devam ediyor mu bayragi
static SystemConfig _config;                // Sistem konfigurasyonu
static char _keyboardBuffer[32] = "";       // Klavye giris tamponu
static int _keyboardLen = 0;                // Klavye giris uzunlugu
static char _pendingUID[24] = "";           // Eklenecek personelin UID'si
static int _listPage = 0;                  // Personel listesi sayfa numarasi
static int _deleteIndex = -1;               // Silinecek personel indeksi (-1: secim yok)
static char _lastDate[12] = "";             // Son bilinen tarih (gunluk reset kontrolu)
static int _lastAutoCheckoutMin = -1;       // Otomatik cikis kontrol dakikasi

// =============================================================================
// DAHILI YARDIMCI FONKSIYONLAR
// =============================================================================

// Sistem konfigurasyonunu LittleFS uzerine JSON olarak kaydeder
static bool _saveConfig() {
    StaticJsonDocument<512> doc;
    doc["ssid"] = _config.wifiSSID;
    doc["pass"] = _config.wifiPass;
    doc["adminUID"] = _config.adminUID;

    JsonObject shiftObj = doc.createNestedObject("shift");
    shiftObj["sh"] = _config.shift.startHour;
    shiftObj["sm"] = _config.shift.startMinute;
    shiftObj["eh"] = _config.shift.endHour;
    shiftObj["em"] = _config.shift.endMinute;

    String jsonStr;
    serializeJson(doc, jsonStr);
    bool ok = storageWrite(PATH_CONFIG, jsonStr);
    if (ok) {
        Serial.println(F("[SYSTEM] Konfigurasyon LittleFS'e kaydedildi."));
    } else {
        Serial.println(F("[SYSTEM] HATA: Konfigurasyon LittleFS'e kaydedilemedi!"));
    }
    return ok;
}

// Sistem konfigurasyonunu LittleFS uzerindeki JSON dosyasindan yukler
static void _loadConfig() {
    memset(&_config, 0, sizeof(_config));
    strncpy(_config.wifiSSID, DEFAULT_WIFI_SSID, sizeof(_config.wifiSSID) - 1);
    strncpy(_config.wifiPass, DEFAULT_WIFI_PASS, sizeof(_config.wifiPass) - 1);
    strncpy(_config.adminUID, DEFAULT_ADMIN_UID, sizeof(_config.adminUID) - 1);
    _config.shift.startHour = 8;
    _config.shift.startMinute = 0;
    _config.shift.endHour = 17;
    _config.shift.endMinute = 0;

    if (!storageExists(PATH_CONFIG)) {
        Serial.println(F("[SYSTEM] Konfigurasyon dosyasi bulunamadi, varsayilanlar olusturuluyor."));
        _saveConfig();
        return;
    }

    String jsonStr = storageRead(PATH_CONFIG);
    if (jsonStr.length() == 0) {
        Serial.println(F("[SYSTEM] Konfigurasyon dosyasi bos, varsayilanlar kullanilacak."));
        return;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        Serial.printf("[SYSTEM] JSON parse hatasi: %s. Varsayilanlar kullanilacak.\n", error.c_str());
        return;
    }

    if (doc.containsKey("ssid")) {
        const char* s = doc["ssid"];
        if (s) strncpy(_config.wifiSSID, s, sizeof(_config.wifiSSID) - 1);
    }
    if (doc.containsKey("pass")) {
        const char* p = doc["pass"];
        if (p) strncpy(_config.wifiPass, p, sizeof(_config.wifiPass) - 1);
    }
    if (doc.containsKey("adminUID")) {
        const char* a = doc["adminUID"];
        if (a) strncpy(_config.adminUID, a, sizeof(_config.adminUID) - 1);
    }
    if (doc.containsKey("shift")) {
        JsonObject shiftObj = doc["shift"];
        _config.shift.startHour = shiftObj["sh"] | 8;
        _config.shift.startMinute = shiftObj["sm"] | 0;
        _config.shift.endHour = shiftObj["eh"] | 17;
        _config.shift.endMinute = shiftObj["em"] | 0;
    }

    Serial.printf("[SYSTEM] Yapilandirma yuklendi: SSID=%s, AdminUID=%s, Mesai=%02d:%02d-%02d:%02d\n",
                  _config.wifiSSID, _config.adminUID,
                  _config.shift.startHour, _config.shift.startMinute,
                  _config.shift.endHour, _config.shift.endMinute);
}

// Ana ekrana donus ve ekranin yeniden cizilmesi
static void _returnToMain() {
    _currentScreen = SCR_MAIN;
    _processing = false;
    _selectedEmployee = -1;
    _deleteIndex = -1;
    _pendingUID[0] = '\0';
    _keyboardBuffer[0] = '\0';
    _keyboardLen = 0;

    // Ana ekrani sifirla (tam yeniden cizim)
    uiResetMainScreen();

    char timeBuf[12] = "";
    if (timeIsValid()) {
        timeFormat(timeBuf, timeNow());
    } else {
        strncpy(timeBuf, "--:--", sizeof(timeBuf) - 1);
    }
    uiDrawMainScreen(timeBuf, employeeCount(), employeeActiveCount(), wifiIsConnected());
    _lastMainRefresh = millis();
}

// Mesai bitiminde iceride veya molada kalan personelleri otomatik cikarir
static void _autoCheckoutAll() {
    uint8_t h = timeGetHour();
    uint8_t m = timeGetMinute();
    int currentMin = h * 60 + m;

    if (currentMin == _lastAutoCheckoutMin) {
        return; // Bu dakika icerisinde zaten calisti
    }
    _lastAutoCheckoutMin = currentMin;

    Employee* all = employeeGetAll();
    int totalSlots = employeeGetTotalSlots();
    unsigned long checkoutTime = timeNow();

    for (int i = 0; i < totalSlots; i++) {
        if (!all[i].active) continue;
        if (all[i].state == STATE_MOLADA || all[i].state == STATE_CALISIYOR) {
            ShiftConfig activeShift = all[i].hasCustomShift ? all[i].customShift : _config.shift;
            int empShiftEndMin = activeShift.endHour * 60 + activeShift.endMinute;
            if (currentMin >= empShiftEndMin) {
                employeeSetState(i, STATE_DISARIDA, checkoutTime);
                eventLog(all[i].uid, all[i].name, EVT_OTO_CIKIS, checkoutTime);
                Serial.printf("[SYSTEM] Otomatik cikis yapildi: %s (%s) [Mesai: %02d:%02d]\n",
                              all[i].name, all[i].uid, activeShift.endHour, activeShift.endMinute);
            }
        }
    }
}

// =============================================================================
// SISTEM KONTROLCUSU TEMEL FONKSIYONLARI
// =============================================================================

void systemInit() {
    _loadConfig();
    _currentScreen = SCR_MAIN;
    _selectedEmployee = -1;
    _processing = false;
    _lastCardUID[0] = '\0';
    _lastCardTime = 0;
    _deleteIndex = -1;
    _pendingUID[0] = '\0';
    _keyboardBuffer[0] = '\0';
    _keyboardLen = 0;
    _lastAutoCheckoutMin = -1;

    // Baslangic tarihini kaydet
    if (timeIsValid()) {
        dateFormat(_lastDate, timeNow());
    } else {
        _lastDate[0] = '\0';
    }

    // Ana ekrani cizerek sistemi baslat
    _returnToMain();
    Serial.println(F("[SYSTEM] Sistem kontrolcusu baslatildi."));
}

void systemHandleCard(const char* uid) {
    if (!uid || strlen(uid) == 0) return;

    // 0a. HIZLI PERSONEL KAYIT MODU (SCR_REGISTER_CARD):
    if (_currentScreen == SCR_REGISTER_CARD) {
        // Kart zaten kayitli mi?
        if (employeeFind(uid) >= 0) {
            Employee* existing = employeeGet(employeeFind(uid));
            char msg[48];
            snprintf(msg, sizeof(msg), "Kayitli: %s", existing ? existing->name : uid);
            uiDrawErrorScreen(msg);
            buzzerError();
            _currentScreen = SCR_INFO;
            _screenTimer = millis();
            return;
        }

        // Saat bilgisiyle isim olustur: "Yeni_14:32"
        char autoName[32];
        unsigned long now = timeNow();
        int h = (now % 86400) / 3600;
        int m = ((now % 86400) % 3600) / 60;
        snprintf(autoName, sizeof(autoName), "Yeni_%02d:%02d", h, m);

        // Personeli ekle
        if (employeeAdd(uid, autoName)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Kart kaydedildi!\nWeb panelden isim duzenleyin");
            uiDrawInfoScreen(autoName, "Kart kaydedildi!", COLOR_SUCCESS);
            buzzerSuccess();
            Serial.printf("[SYS] Hizli kayit: %s -> %s\n", uid, autoName);
            // Firebase Firestore'a aninda yukle
            syncUploadNewEmployee(uid, autoName);
            _currentScreen = SCR_INFO;
            _screenTimer = millis();
        } else {
            uiDrawErrorScreen("Kayit basarisiz!");
            buzzerError();
            _currentScreen = SCR_INFO;
            _screenTimer = millis();
        }
        return;
    }

    // 0b. ADMIN_ADD EKRANI KONTROLU (Yeni kart bekleniyor modu):
    if (_currentScreen == SCR_ADMIN_ADD && _processing) {
        if (employeeFind(uid) >= 0) {
            uiDrawErrorScreen("Bu kart kayitli!");
            buzzerError();
            _currentScreen = SCR_INFO;
            _screenTimer = millis();
            return;
        }

        // Yeni kart UID'sini sakla ve isim girisi oncesi onay ekranini ciz
        strncpy(_pendingUID, uid, sizeof(_pendingUID) - 1);
        _pendingUID[sizeof(_pendingUID) - 1] = '\0';
        _processing = false;
        uiDrawAddEmployeeScreen(_pendingUID);
        buzzerSuccess();
        return;
    }

    // 1. SECIM EKRANINDA AYNI KART ILE IPTAL KONTROLU:
    if (_currentScreen == SCR_SELECTION && _selectedEmployee >= 0) {
        Employee* selEmp = employeeGet(_selectedEmployee);
        if (selEmp != nullptr && strcmp(uid, selEmp->uid) == 0) {
            // Eger kullanici karti okuttuktan hemen sonra basili tutuyorsa yoksay
            if (millis() - _lastCardTime > 700) {
                buzzerWarning();
                _returnToMain();
                _lastCardTime = millis();
                return;
            }
            return;
        }
    }

    // 2. DEBOUNCE KONTROLU:
    if (strcmp(uid, _lastCardUID) == 0 && (millis() - _lastCardTime < DEBOUNCE_MS)) {
        return;
    }
    strncpy(_lastCardUID, uid, sizeof(_lastCardUID) - 1);
    _lastCardUID[sizeof(_lastCardUID) - 1] = '\0';
    _lastCardTime = millis();

    // 3. YONETICI (ADMIN) KARTI KONTROLU:
    if (strcmp(uid, _config.adminUID) == 0) {
        _currentScreen = SCR_ADMIN;
        _processing = false;
        uiDrawAdminMenu();
        buzzerSuccess();
        return;
    }

    // 4. PERSONEL ARAMA:
    int idx = employeeFind(uid);
    if (idx == -1) {
        uiDrawErrorScreen("Taninmayan Kart!");
        _currentScreen = SCR_INFO;
        _screenTimer = millis();
        _processing = false;
        buzzerError();
        return;
    }

    // 5. PERSONEL DURUM MAKINESI:
    Employee* emp = employeeGet(idx);
    if (!emp || !emp->active) {
        uiDrawErrorScreen("Gecersiz Personel!");
        _currentScreen = SCR_INFO;
        _screenTimer = millis();
        _processing = false;
        buzzerError();
        return;
    }

    switch (emp->state) {
        case STATE_DISARIDA: {
            // MESAI SAATI GECERLILIK KONTROLU (SECENEK 3 KURALI)
            if (timeIsValid()) {
                uint8_t curH = timeGetHour();
                uint8_t curM = timeGetMinute();
                int currentMins = curH * 60 + curM;
                ShiftConfig activeShift = emp->hasCustomShift ? emp->customShift : _config.shift;
                int shiftStartMins = activeShift.startHour * 60 + activeShift.startMinute;
                int shiftEndMins = activeShift.endHour * 60 + activeShift.endMinute;
                int earliestAllowed = shiftStartMins - SHIFT_START_EARLY_TOLERANCE;

                // 1. Kural: Mesai baslangicindan 30 dakikadan daha erken mi gelindi?
                if (currentMins < earliestAllowed) {
                    char errBuf[48];
                    snprintf(errBuf, sizeof(errBuf), "Erken! Baslangic %02d:%02d", 
                             activeShift.startHour, activeShift.startMinute);
                    uiDrawErrorScreen(errBuf);
                    buzzerError();
                    _currentScreen = SCR_INFO;
                    _screenTimer = millis();
                    _processing = false;
                    return;
                }

                // 2. Kural: Mesai bitis saati gecti mi?
                if (currentMins >= shiftEndMins) {
                    uiDrawErrorScreen("Mesai Bitti! Giris Yok");
                    buzzerError();
                    _currentScreen = SCR_INFO;
                    _screenTimer = millis();
                    _processing = false;
                    return;
                }
            }

            // OTOMATIK GIRIS ISLEMI
            employeeSetState(idx, STATE_CALISIYOR, timeNow());
            eventLog(uid, emp->name, EVT_GIRIS, timeNow());
            uiDrawInfoScreen(emp->name, "Hos geldin!", COLOR_SUCCESS);
            buzzerSuccess();
            _currentScreen = SCR_INFO;
            _screenTimer = millis();
            _processing = false;
            break;
        }

        case STATE_CALISIYOR:
            // SECIM EKRANINI AC (MOLA / CIKIS)
            _selectedEmployee = idx;
            uiDrawSelectionScreen(emp->name);
            _currentScreen = SCR_SELECTION;
            _screenTimer = millis();
            _processing = true;
            break;

        case STATE_MOLADA:
            // OTOMATIK MOLADAN DONUS ISLEMI
            employeeSetState(idx, STATE_CALISIYOR, timeNow());
            eventLog(uid, emp->name, EVT_MOLA_BIT, timeNow());
            uiDrawInfoScreen(emp->name, "Moladan donus!", COLOR_SUCCESS);
            buzzerSuccess();
            _currentScreen = SCR_INFO;
            _screenTimer = millis();
            _processing = false;
            break;
    }
}

void systemHandleTouch(int16_t x, int16_t y) {
    switch (_currentScreen) {
        case SCR_SELECTION: {
            int choice = uiCheckSelectionTouch(x, y);
            if (choice == 1) { // MOLA SECILDI
                uint8_t h = timeGetHour();
                uint8_t m = timeGetMinute();
                int currentMin = h * 60 + m;

                Employee* emp = employeeGet(_selectedEmployee);
                if (emp != nullptr) {
                    ShiftConfig activeShift = emp->hasCustomShift ? emp->customShift : _config.shift;
                    int shiftEndMin = activeShift.endHour * 60 + activeShift.endMinute;

                    // Mesai bitimine yakin mola istegi -> Otomatik Cikis olarak isle
                    if ((shiftEndMin - currentMin <= SHIFT_END_TOLERANCE) && (shiftEndMin - currentMin >= 0)) {
                        employeeSetState(_selectedEmployee, STATE_DISARIDA, timeNow());
                        eventLog(emp->uid, emp->name, EVT_CIKIS, timeNow());
                        uiDrawInfoScreen(emp->name, "Mesai bitti, cikis!", COLOR_WARNING);
                        buzzerWarning();
                    } else {
                        // Normal mola baslangici
                        employeeSetState(_selectedEmployee, STATE_MOLADA, timeNow());
                        eventLog(emp->uid, emp->name, EVT_MOLA_BAS, timeNow());
                        uiDrawInfoScreen(emp->name, "Iyi molalar!", COLOR_SUCCESS);
                        buzzerSuccess();
                    }
                }
                _currentScreen = SCR_INFO;
                _screenTimer = millis();
                _processing = false;
                _selectedEmployee = -1;
            } else if (choice == 2) { // CIKIS SECILDI
                Employee* emp = employeeGet(_selectedEmployee);
                if (emp != nullptr) {
                    employeeSetState(_selectedEmployee, STATE_DISARIDA, timeNow());
                    eventLog(emp->uid, emp->name, EVT_CIKIS, timeNow());
                    uiDrawInfoScreen(emp->name, "Gule gule!", COLOR_SUCCESS);
                    buzzerSuccess();
                }
                _currentScreen = SCR_INFO;
                _screenTimer = millis();
                _processing = false;
                _selectedEmployee = -1;
            }
            break;
        }

        case SCR_ADMIN: {
            int choice = uiCheckAdminTouch(x, y);
            switch (choice) {
                case 0: // Personel Ekle
                    _pendingUID[0] = '\0';
                    uiDrawInfoScreen("", "Yeni kart okutun", COLOR_LIGHT_BLUE);
                    _currentScreen = SCR_ADMIN_ADD;
                    _processing = true; // Yeni kart dokundurulmasini bekle
                    break;
                case 1: // Personel Sil
                    _listPage = 0;
                    _deleteIndex = -1;
                    uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                    _currentScreen = SCR_ADMIN_DELETE;
                    break;
                case 2: // Mesai Ayarla
                    uiDrawShiftSettings(_config.shift);
                    _currentScreen = SCR_ADMIN_SHIFT;
                    break;
                case 3: // Personel Liste
                    _listPage = 0;
                    uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                    _currentScreen = SCR_ADMIN_LIST;
                    break;
                case 4: // Geri
                    _returnToMain();
                    break;
            }
            break;
        }

        case SCR_ADMIN_ADD: {
            int choice = uiCheckAddEmployeeTouch(x, y);
            if (choice == 1) { // EKLE - Isim girmek uzere klavyeye gec
                if (strlen(_pendingUID) > 0) {
                    memset(_keyboardBuffer, 0, sizeof(_keyboardBuffer));
                    _keyboardLen = 0;
                    uiDrawKeyboard(_keyboardBuffer);
                    _currentScreen = SCR_KEYBOARD;
                }
            } else if (choice == 2) { // GERI
                _processing = false;
                _pendingUID[0] = '\0';
                uiDrawAdminMenu();
                _currentScreen = SCR_ADMIN;
            }
            break;
        }

        case SCR_KEYBOARD: {
            char c = uiCheckKeyboardTouch(x, y);
            if (c == '\n') { // TAMAM / ONAY
                if (_keyboardLen > 0) {
                    bool ok = employeeAdd(_pendingUID, _keyboardBuffer);
                    if (ok) {
                        syncUploadNewEmployee(_pendingUID, _keyboardBuffer);
                        uiDrawInfoScreen(_keyboardBuffer, "Personel eklendi!", COLOR_SUCCESS);
                        buzzerSuccess();
                    } else {
                        uiDrawErrorScreen("Personel eklenemedi!");
                        buzzerError();
                    }
                    _pendingUID[0] = '\0';
                    _currentScreen = SCR_INFO;
                    _screenTimer = millis();
                    _processing = false;
                }
            } else if (c == '\b') { // SIL (Backspace)
                if (_keyboardLen > 0) {
                    _keyboardLen--;
                    _keyboardBuffer[_keyboardLen] = '\0';
                    uiDrawKeyboard(_keyboardBuffer);
                }
            } else if (c != 0 && _keyboardLen < 30) { // Harf / Karakter girisi
                _keyboardBuffer[_keyboardLen++] = c;
                _keyboardBuffer[_keyboardLen] = '\0';
                uiDrawKeyboard(_keyboardBuffer);
            }
            break;
        }

        case SCR_ADMIN_LIST: {
            int choice = uiCheckListTouch(x, y, _listPage, employeeGetTotalSlots(), employeeGetAll());
            if (choice == -4) { // Geri
                uiDrawAdminMenu();
                _currentScreen = SCR_ADMIN;
            } else if (choice == -2) { // Onceki sayfa
                if (_listPage > 0) {
                    _listPage--;
                    uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                }
            } else if (choice == -3) { // Sonraki sayfa
                int maxPages = (employeeCount() + 3) / 4;
                if (_listPage + 1 < maxPages) {
                    _listPage++;
                    uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                }
            }
            break;
        }

        case SCR_ADMIN_DELETE: {
            if (_deleteIndex >= 0) {
                // Silme onay ekranindayiz (EVET / HAYIR)
                int dc = uiCheckDeleteTouch(x, y);
                if (dc == 1) { // EVET - Sil
                    employeeRemove(_deleteIndex);
                    buzzerSuccess();
                    _deleteIndex = -1;
                    int maxPages = (employeeCount() + 3) / 4;
                    if (_listPage >= maxPages && _listPage > 0) {
                        _listPage = maxPages > 0 ? maxPages - 1 : 0;
                    }
                    uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                } else if (dc == 2) { // HAYIR - Iptal
                    _deleteIndex = -1;
                    uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                }
            } else {
                // Silinecek personel secim listesindeyiz
                int choice = uiCheckListTouch(x, y, _listPage, employeeGetTotalSlots(), employeeGetAll());
                if (choice >= 0) {
                    _deleteIndex = choice;
                    Employee* emp = employeeGet(choice);
                    if (emp != nullptr && emp->active) {
                        uiDrawDeleteConfirm(emp->name);
                    } else {
                        _deleteIndex = -1;
                    }
                } else if (choice == -4) { // Geri
                    uiDrawAdminMenu();
                    _currentScreen = SCR_ADMIN;
                } else if (choice == -2) { // Onceki sayfa
                    if (_listPage > 0) {
                        _listPage--;
                        uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                    }
                } else if (choice == -3) { // Sonraki sayfa
                    int maxPages = (employeeCount() + 3) / 4;
                    if (_listPage + 1 < maxPages) {
                        _listPage++;
                        uiDrawEmployeeList(employeeGetAll(), employeeGetTotalSlots(), _listPage);
                    }
                }
            }
            break;
        }

        case SCR_ADMIN_SHIFT: {
            int choice = uiCheckShiftTouch(x, y);
            switch (choice) {
                case 0: _config.shift.startHour = (_config.shift.startHour + 1) % 24; break;
                case 1: _config.shift.startHour = (_config.shift.startHour + 23) % 24; break;
                case 2: _config.shift.startMinute = (_config.shift.startMinute + 5) % 60; break;
                case 3: _config.shift.startMinute = (_config.shift.startMinute + 55) % 60; break;
                case 4: _config.shift.endHour = (_config.shift.endHour + 1) % 24; break;
                case 5: _config.shift.endHour = (_config.shift.endHour + 23) % 24; break;
                case 6: _config.shift.endMinute = (_config.shift.endMinute + 5) % 60; break;
                case 7: _config.shift.endMinute = (_config.shift.endMinute + 55) % 60; break;
                case 8: // Kaydet
                    _saveConfig();
                    buzzerSuccess();
                    uiDrawInfoScreen("", "Ayarlar kaydedildi!", COLOR_SUCCESS);
                    _currentScreen = SCR_INFO;
                    _screenTimer = millis();
                    break;
                case 9: // Geri
                    uiDrawAdminMenu();
                    _currentScreen = SCR_ADMIN;
                    break;
            }
            if (choice >= 0 && choice <= 7) {
                uiDrawShiftSettings(_config.shift);
            }
            break;
        }

        case SCR_INFO:
            // Bilgi ekraninda herhangi bir yere dokunulursa ana ekrana hizli donus
            _returnToMain();
            break;

        case SCR_MAIN:
            // Ana ekrandaki "+ PERSONEL" butonuna dokunuldu mu?
            if (uiCheckRegisterButtonTouch(x, y)) {
                _currentScreen = SCR_REGISTER_CARD;
                _screenTimer = millis();
                uiDrawRegisterCardScreen();
                buzzerSuccess();
            }
            // Ana ekrandaki "SENKRONIZE" butonuna dokunuldu mu?
            else if (uiCheckSyncButtonTouch(x, y)) {
                buzzerStop();
                uiDrawInfoScreen("", "Senkronize Ediliyor...", 0x03E0);
                _currentScreen = SCR_INFO;
                _screenTimer = millis();
                if (wifiIsConnected()) {
                    syncDownloadSettings();
                    syncDownloadEmployees();
                    syncUploadPendingEvents();
                    uiDrawInfoScreen("", "Senkronizasyon Basarili!", COLOR_SUCCESS);
                    buzzerSuccess();
                } else {
                    uiDrawErrorScreen("WiFi Baglantisi Yok!");
                    buzzerError();
                }
                _screenTimer = millis();
            }
            break;

        case SCR_REGISTER_CARD:
            // Iptal butonu (80, 192, 160x36) dokunma alani
            if (x >= 75 && x <= 245 && y >= 186 && y <= 232) {
                buzzerWarning();
                _returnToMain();
            }
            break;

        default:
            break;
    }
}

void systemTick() {
    // 1. EKRAN TIMEOUT KONTROLLERİ:
    if (_currentScreen == SCR_INFO && (millis() - _screenTimer > INFO_DISPLAY_MS)) {
        _returnToMain();
    }
    if (_currentScreen == SCR_SELECTION && (millis() - _screenTimer > SELECTION_TIMEOUT)) {
        _processing = false;
        _selectedEmployee = -1;
        buzzerWarning();
        _returnToMain();
    }
    // Personel kayit ekrani timeout (15 saniye)
    if (_currentScreen == SCR_REGISTER_CARD && (millis() - _screenTimer > 15000)) {
        _returnToMain();
    }

    // 2. ANA EKRAN PERIYODIK YENILEME:
    if (_currentScreen == SCR_MAIN && (millis() - _lastMainRefresh > MAIN_SCREEN_REFRESH)) {
        _lastMainRefresh = millis();
        char timeBuf[12] = "";
        if (timeIsValid()) {
            timeFormat(timeBuf, timeNow());
        } else {
            strncpy(timeBuf, "--:--", sizeof(timeBuf) - 1);
        }
        uiDrawMainScreen(timeBuf, employeeCount(), employeeActiveCount(), wifiIsConnected());
    }

    // 3. MESAI BITIS OTOMATIK CIKIS KONTROLU:
    if (timeIsValid()) {
        _autoCheckoutAll();
    }

    // 4. GUNLUK SIFIRLAMA KONTROLU (Gece yarisi yeni gune gecis):
    if (timeIsValid()) {
        char dateBuf[12] = "";
        dateFormat(dateBuf, timeNow());
        if (strlen(dateBuf) > 0) {
            if (_lastDate[0] == '\0') {
                strncpy(_lastDate, dateBuf, sizeof(_lastDate) - 1);
            } else if (strcmp(dateBuf, _lastDate) != 0) {
                Serial.printf("[SYSTEM] Yeni gun basladi: %s (Onceki: %s)\n", dateBuf, _lastDate);
                strncpy(_lastDate, dateBuf, sizeof(_lastDate) - 1);
                employeeResetDailyCounters();
                eventCleanup();
            }
        }
    }
}

ScreenState systemGetScreen() {
    return _currentScreen;
}

bool systemIsProcessing() {
    return _processing;
}

void systemSetWifi(const char* ssid, const char* pass) {
    if (ssid && strlen(ssid) > 0) {
        strncpy(_config.wifiSSID, ssid, sizeof(_config.wifiSSID) - 1);
        _config.wifiSSID[sizeof(_config.wifiSSID) - 1] = '\0';
    }
    if (pass) {
        strncpy(_config.wifiPass, pass, sizeof(_config.wifiPass) - 1);
        _config.wifiPass[sizeof(_config.wifiPass) - 1] = '\0';
    }
    _saveConfig();
    Serial.printf("[SYSTEM] WiFi yapilandirmasi guncellendi: SSID=%s\n", _config.wifiSSID);
    wifiInit(_config.wifiSSID, _config.wifiPass);
}

void systemSetAdminUID(const char* uid) {
    if (uid && strlen(uid) > 0) {
        strncpy(_config.adminUID, uid, sizeof(_config.adminUID) - 1);
        _config.adminUID[sizeof(_config.adminUID) - 1] = '\0';
        _saveConfig();
        Serial.printf("[SYSTEM] Admin UID guncellendi: %s\n", _config.adminUID);
    }
}

void systemSetShift(const ShiftConfig& shift) {
    _config.shift = shift;
    _saveConfig();
    Serial.printf("[SYSTEM] Yeni mesai saatleri kaydedildi: %02d:%02d - %02d:%02d\n",
                  _config.shift.startHour, _config.shift.startMinute,
                  _config.shift.endHour, _config.shift.endMinute);
}

ShiftConfig systemGetShift() {
    return _config.shift;
}

void systemPrintStatus() {
    Serial.println(F("\n========== SISTEM DURUMU =========="));
    Serial.printf("Ekran Durumu: %d\n", (int)_currentScreen);
    Serial.printf("WiFi Bagli mi: %s (IP: %s)\n", wifiIsConnected() ? "EVET" : "HAYIR", wifiGetIP().c_str());
    Serial.printf("NFC Durumu: %s\n", nfcIsAvailable() ? "AKTIF" : "BAGLI DEGIL");
    char timeBuf[12] = "";
    if (timeIsValid()) {
        timeFormat(timeBuf, timeNow());
        char dateBuf[12] = "";
        dateFormat(dateBuf, timeNow());
        Serial.printf("Zaman: %s %s\n", dateBuf, timeBuf);
    } else {
        Serial.println(F("Zaman: SENKRONIZE DEGIL"));
    }
    Serial.printf("Toplam Personel: %d\n", employeeCount());
    Serial.printf("Icerideki Personel: %d\n", employeeActiveCount());
    Serial.printf("Bekleyen Olaylar: %d\n", eventPendingCount());
    Serial.printf("Bosta Depolama: %lu byte\n", storageGetFreeSpace());
    Serial.printf("Mesai Saatleri: %02d:%02d - %02d:%02d\n",
                  _config.shift.startHour, _config.shift.startMinute,
                  _config.shift.endHour, _config.shift.endMinute);
    Serial.printf("Admin UID: %s\n", _config.adminUID);
    Serial.println(F("===================================\n"));
}
