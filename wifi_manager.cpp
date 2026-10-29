#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>

// ==========================================
// Dahili degiskenler
// ==========================================
static char storedSSID[64] = {0};
static char storedPassword[64] = {0};
static bool _wifiEnabled = false;
static bool _wasConnected = false;
static unsigned long _lastReconnectAttempt = 0;
static unsigned long _connectStartTime = 0;
static int _reconnectCount = 0;
static const int MAX_RECONNECT_BEFORE_RESET = 5;

// ==========================================
// WiFi Event Handler - baglanti olaylarini yakala
// ==========================================
static void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WiFi] Baglandi! IP: ");
      Serial.println(WiFi.localIP());
      _wasConnected = true;
      _reconnectCount = 0;
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (_wasConnected) {
        Serial.println("[WiFi] Baglanti koptu! Otomatik yeniden baglanma...");
        _wasConnected = false;
      }
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WiFi] AP'ye baglandi, IP bekleniyor...");
      break;

    default:
      break;
  }
}

// ==========================================
// _startConnection - WiFi baglantisinini baslat
// ==========================================
static void _startConnection() {
  if (strlen(storedSSID) == 0) return;

  WiFi.disconnect(true);  // Onceki baglantilari temizle
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);     // Otomatik yeniden baglanma ACIK
  WiFi.persistent(true);           // Kimlik bilgilerini flash'a kaydet

  // ESP32 WiFi guc yonetimi kapat - daha kararli baglanti
  esp_wifi_set_ps(WIFI_PS_NONE);

  WiFi.begin(storedSSID, storedPassword);
  _connectStartTime = millis();

  Serial.printf("[WiFi] Baglaniliyor: %s\n", storedSSID);
}

// ==========================================
// wifiInit - WiFi baslatma (hizli, non-blocking)
// ==========================================
void wifiInit(const char* ssid, const char* password) {
  // Event handler kaydet
  WiFi.onEvent(onWiFiEvent);

  if (ssid == nullptr || strlen(ssid) == 0) {
    Serial.println("[WiFi] SSID tanimli degil. WiFi devre disi.");
    storedSSID[0] = '\0';
    storedPassword[0] = '\0';
    _wifiEnabled = false;
    return;
  }

  strncpy(storedSSID, ssid, sizeof(storedSSID) - 1);
  storedSSID[sizeof(storedSSID) - 1] = '\0';

  if (password != nullptr) {
    strncpy(storedPassword, password, sizeof(storedPassword) - 1);
    storedPassword[sizeof(storedPassword) - 1] = '\0';
  } else {
    storedPassword[0] = '\0';
  }

  _wifiEnabled = true;
  _wasConnected = false;
  _reconnectCount = 0;

  // Baglantiya basla
  _startConnection();

  // Ilk baglanti icin KISA bekleme (max 5 saniye)
  // Baglanamasa bile devam eder, arka planda tekrar dener
  Serial.print("[WiFi] Ilk baglanti bekleniyor (max 5sn)...");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 5000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Baglandi! IP: ");
    Serial.println(WiFi.localIP());
    _wasConnected = true;
  } else {
    Serial.println("[WiFi] Ilk baglanti basarisiz - arka planda denenecek");
  }
}

// ==========================================
// wifiTick - Periyodik WiFi kontrolu
// Her loop'ta cagirilir, non-blocking
// ==========================================
void wifiTick() {
  if (!_wifiEnabled || strlen(storedSSID) == 0) return;

  // Zaten bagliysa hicbir sey yapma
  if (WiFi.status() == WL_CONNECTED) {
    if (!_wasConnected) {
      _wasConnected = true;
      _reconnectCount = 0;
    }
    return;
  }

  // Bagli degilse - yeniden baglanma
  unsigned long now = millis();

  // Reconnect araligi: ilk 3 denemede 5sn, sonra 15sn, 5 basarisiz denemeden sonra full reset
  unsigned long retryInterval = (_reconnectCount < 3) ? 5000 : 15000;

  if (now - _lastReconnectAttempt >= retryInterval) {
    _lastReconnectAttempt = now;
    _reconnectCount++;

    if (_reconnectCount >= MAX_RECONNECT_BEFORE_RESET) {
      // 5 basarisiz deneme sonrasi tam WiFi reset
      Serial.printf("[WiFi] %d basarisiz deneme - WiFi tam reset\n", _reconnectCount);
      _reconnectCount = 0;
      _startConnection();  // disconnect + yeniden begin
    } else {
      Serial.printf("[WiFi] Yeniden baglanma denemesi #%d\n", _reconnectCount);
      WiFi.reconnect();
    }
  }
}

// ==========================================
// wifiIsConnected - Baglanti durumu
// ==========================================
bool wifiIsConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

// ==========================================
// wifiGetIP - IP adresi
// ==========================================
String wifiGetIP() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return String("0.0.0.0");
}

// ==========================================
// wifiUpdateCredentials - SSID/sifre guncelle
// ==========================================
void wifiUpdateCredentials(const char* ssid, const char* password) {
  wifiInit(ssid, password);
}
