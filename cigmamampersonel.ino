/*
 * CIGMAMAM PERSONEL GIRIS-CIKIS TAKIP SISTEMI
 * ============================================
 * Donanim:
 *   - ESP32-WROOM 30pin
 *   - ILI9341 2.8" TFT + XPT2046 Dokunmatik
 *   - PN532 NFC/RFID (I2C)
 *   - Pasif Buzzer (GPIO32)
 *
 * Gerekli Kutuphaneler:
 *   - Adafruit GFX Library
 *   - Adafruit ILI9341
 *   - XPT2046_Touchscreen
 *   - Adafruit PN532
 *   - ArduinoJson (v6)
 *   - LittleFS (ESP32 dahili)
 */

#include "config.h"
#include "buzzer.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "time_manager.h"
#include "nfc_reader.h"
#include "display_manager.h"
#include "touch_ui.h"
#include "employee_manager.h"
#include "event_logger.h"
#include "system_controller.h"
#include "firebase_sync.h"
#include <esp_task_wdt.h>

// Watchdog zaman asimi suresi (saniye)
#define WDT_TIMEOUT 30

// Eger config.h icerisinde tanimli degilse guvenli varsayilanlar
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID "G514"
#endif

#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS "jkaf4tpn9g"
#endif

// Seri port komut tamponu
static char _serialBuffer[128];
static uint8_t _serialIndex = 0;

// =============================================================================
// SERI PORT KOMUT ISLEYICI
// =============================================================================
// Web arayuzu oncesinde ilk kurulum ve testleri kolaylastirmak icin seri port komutlari
static void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      _serialBuffer[_serialIndex] = '\0';
      if (_serialIndex > 0) {
        if (strncmp(_serialBuffer, "wifi:", 5) == 0) {
          // Format: wifi:SSID:PASSWORD
          char* ssid = _serialBuffer + 5;
          char* pass = strchr(ssid, ':');
          if (pass != nullptr) {
            *pass = '\0';
            pass++;
            systemSetWifi(ssid, pass);
            Serial.printf("[SERI] WiFi ayarlandi: SSID='%s'\n", ssid);
          } else {
            systemSetWifi(ssid, "");
            Serial.printf("[SERI] WiFi sifresiz ayarlandi: SSID='%s'\n", ssid);
          }
        } else if (strncmp(_serialBuffer, "admin:", 6) == 0) {
          // Format: admin:UID
          const char* uid = _serialBuffer + 6;
          systemSetAdminUID(uid);
          Serial.printf("[SERI] Admin UID ayarlandi: %s\n", uid);
        } else if (strcmp(_serialBuffer, "time") == 0) {
          if (timeIsValid()) {
            char tBuf[12] = "";
            char dBuf[12] = "";
            timeFormat(tBuf, timeNow());
            dateFormat(dBuf, timeNow());
            Serial.printf("[SERI] Guncel Zaman: %s %s (Saat: %02d, Dakika: %02d)\n",
                          dBuf, tBuf, timeGetHour(), timeGetMinute());
          } else {
            Serial.println(F("[SERI] Zaman henuz senkronize degil!"));
          }
        } else if (strcmp(_serialBuffer, "status") == 0) {
          systemPrintStatus();
        } else if (strcmp(_serialBuffer, "sync") == 0) {
          Serial.println(F("[SERI] Manuel sync tetiklendi..."));
          syncUploadAllEmployees();
          syncUploadPendingEvents();
          Serial.println(F("[SERI] Sync tamamlandi."));
        } else if (strcmp(_serialBuffer, "employees") == 0) {
          int total = employeeGetTotalSlots();
          Employee* list = employeeGetAll();
          Serial.println(F("\n--- KAYITLI PERSONELLER ---"));
          int count = 0;
          for (int i = 0; i < total; i++) {
            if (list[i].active) {
              Serial.printf("[%02d] UID: %-16s | Isim: %-16s | Durum: %s | Mola: %d\n",
                            i, list[i].uid, list[i].name, stateToStr(list[i].state), list[i].breakCount);
              count++;
            }
          }
          Serial.printf("Toplam %d aktif personel kayitli.\n---------------------------\n\n", count);
        } else if (strcmp(_serialBuffer, "help") == 0) {
          Serial.println(F("\n--- KULLANILABILIR SERI KOMUTLAR ---"));
          Serial.println(F("wifi:SSID:PASSWORD -> WiFi ag adi ve sifresini ayarlar"));
          Serial.println(F("admin:UID          -> Yonetici kart UID'sini ayarlar"));
          Serial.println(F("time               -> Guncel saat ve tarih bilgisini gosterir"));
          Serial.println(F("status             -> Sistem genel durumunu listeler"));
          Serial.println(F("employees          -> Kayitli tum personelleri listeler"));
          Serial.println(F("------------------------------------\n"));
        } else {
          Serial.printf("[SERI] Bilinmeyen komut: '%s'. Yardim icin 'help' yazin.\n", _serialBuffer);
        }
      }
      _serialIndex = 0;
    } else if (_serialIndex < sizeof(_serialBuffer) - 1) {
      _serialBuffer[_serialIndex++] = c;
    }
  }
}

// =============================================================================
// SETUP - BASLANGIC YAPILANDIRMASI
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500); // Boot ve guc stabilizasyonu

  Serial.println();
  Serial.println(F("================================"));
  Serial.println(F("  CIGMAMAM PERSONEL SISTEMI"));
  Serial.println(F("================================"));

  // 1. Depolama baslat (LittleFS)
  Serial.print(F("LittleFS baslatiliyor... "));
  if (storageInit()) {
    Serial.println(F("OK"));
  } else {
    Serial.println(F("HATA!"));
  }

  // 2. Ekran baslat (ILI9341)
  Serial.print(F("Ekran baslatiliyor... "));
  displayInit();
  displayClear();
  displayCentered(100, "Baslatiliyor...", COLOR_TEXT, 2);
  Serial.println(F("OK"));

  // 3. Dokunmatik panel baslat (XPT2046)
  Serial.print(F("Dokunmatik baslatiliyor... "));
  touchInit();
  Serial.println(F("OK"));

  // 4. Buzzer baslat (PWM / Ton)
  Serial.print(F("Buzzer baslatiliyor... "));
  buzzerInit();
  Serial.println(F("OK"));

  // 5. WiFi baslat
  Serial.print(F("WiFi baslatiliyor... "));
  wifiInit(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
  Serial.println(F("OK"));

  // 6. NFC okuyucu baslat (PN532 I2C)
  Serial.print(F("NFC baslatiliyor... "));
  if (nfcInit()) {
    Serial.println(F("OK"));
  } else {
    Serial.println(F("HATA! NFC bulunamadi."));
  }

  // 7. Zaman yoneticisi baslat (NTP & Dahili saat)
  Serial.print(F("Zaman senkronize ediliyor... "));
  timeInit();
  if (timeIsValid()) {
    char buf[12];
    timeFormat(buf, timeNow());
    Serial.printf("OK - %s\n", buf);
  } else {
    Serial.println(F("Zaman henuz gecerli degil (NTP bekleniyor)"));
  }

  // 8. Personel verilerini LittleFS'ten yukle
  Serial.print(F("Personel verileri yukleniyor... "));
  employeeInit();
  Serial.printf("%d personel yuklendi\n", employeeCount());

  // 9. Olay kaydediciyi baslat
  Serial.print(F("Olay kaydedicisi baslatiliyor... "));
  eventInit();
  Serial.printf("%d bekleyen olay\n", eventPendingCount());

  // 10. Sistem kontrolcusunu baslat (Config yukleme & Ana ekran cizimi)
  Serial.print(F("Sistem kontrolcusu baslatiliyor... "));
  systemInit();
  Serial.println(F("OK"));

  // 11. Firebase senkronizasyon baslat
  Serial.print(F("Firebase sync baslatiliyor... "));
  syncInit();
  Serial.println(F("OK"));

  // Watchdog baslat (ESP-IDF 2.x standardi)
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  Serial.println();
  Serial.println(F("Sistem hazir!"));
  Serial.println(F("================================"));

  buzzerSuccess();
}

// =============================================================================
// LOOP - ANA CALISMA DONGUSU
// =============================================================================
void loop() {
  // 0. Watchdog zamanlayicisini besle
  esp_task_wdt_reset();

  // 1. WiFi yonetimi (Yeniden baglanma & durum kontrolu)
  wifiTick();

  // 2. Zaman yonetimi (NTP periyodik senkronizasyon & saat guncelleme)
  timeTick();

  // 3. Buzzer ses desenleri guncelleme (Asenkron tonlar)
  buzzerTick();

  // 4. NFC kart okuma islemi
  // Normalde baska bir islem yapilmiyorken kart okunur;
  // ancak yeni kart kayit ekraninda (SCR_ADMIN_ADD) veya secim ekraninda (iptal icin) da kart okunabilir.
  if (!systemIsProcessing() || systemGetScreen() == SCR_ADMIN_ADD || systemGetScreen() == SCR_SELECTION || systemGetScreen() == SCR_REGISTER_CARD) {
    char uidBuf[24];
    if (nfcRead(uidBuf, sizeof(uidBuf))) {
      Serial.printf("[NFC] Kart okundu: %s\n", uidBuf);
      systemHandleCard(uidBuf);
    }
  }

  // 5. Dokunmatik ekran girisi kontrolu
  int16_t tx, ty;
  if (touchRead(tx, ty)) {
    Serial.printf("[TOUCH] Dokunma: x=%d, y=%d\n", tx, ty);
    systemHandleTouch(tx, ty);
  }

  // 6. Sistem kontrolcusu tick (Ekran timeout, ana ekran yenileme, mesai bitisi kontrolu)
  systemTick();

  // 7. Firebase senkronizasyon (olay yukleme, personel/ayar indirme)
  syncTick();

  // 8. Seri port komutlarini isle
  handleSerialCommands();

  // 8. Sistem kararliligi icin kisa bekleme
  delay(10);
}
