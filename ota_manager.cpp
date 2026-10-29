#include "ota_manager.h"
#include "wifi_manager.h"
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "display_manager.h"
#include "touch_ui.h"

// Web tabanli OTA guncellemesi icin port 80 sunucusu
static WebServer otaServer(80);
static bool _otaInitialized = false;

// Web update HTML arayuzu
static const char* serverIndex = 
"<meta charset='utf-8'><title>Çiğmamam Cihaz Güncelleme</title>"
"<style>"
"body{font-family:sans-serif;background:#0f172a;color:#f8fafc;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
".card{background:#1e293b;padding:30px;border-radius:12px;box-shadow:0 8px 24px rgba(0,0,0,0.3);width:340px;text-align:center;}"
"h2{margin-top:0;color:#38bdf8;font-size:20px;}"
"p{font-size:13px;color:#94a3b8;margin-bottom:20px;}"
"input[type=file]{margin:15px 0;width:100%;font-size:12px;color:#cbd5e1;}"
"input[type=submit]{background:#0284c7;color:#fff;border:none;padding:10px 20px;border-radius:6px;cursor:pointer;font-weight:bold;width:100%;font-size:14px;}"
"input[type=submit]:hover{background:#0369a1;}"
"</style>"
"<div class='card'>"
"<h2>Çiğmamam Terminal</h2>"
"<p>Uzaktan Firmware (.bin) Yükleme</p>"
"<form method='POST' action='/update' enctype='multipart/form-data'>"
"<input type='file' name='update' accept='.bin' required><br>"
"<input type='submit' value='Firmware Güncelle'>"
"</form></div>";

void otaInit() {
  // 1. Arduino IDE / PlatformIO Native OTA Yapilandirmasi
  ArduinoOTA.setHostname("cigmamam-terminal");
  ArduinoOTA.setPassword("cigmamam2026"); // Guvenlik sifresi

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("[OTA] Uzaktan guncelleme baslatildi: " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Guncelleme tamamlandi! Cihaz yeniden baslatiliyor...");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Ilerleme: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Hata [%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Yetkilendirme Hatasi");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Baslatma Hatasi");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Baglanti Hatasi");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Veri Alma Hatasi");
    else if (error == OTA_END_ERROR) Serial.println("Bitis Hatasi");
  });

  ArduinoOTA.begin();

  // 2. Web Tarayicisi Uzerinden Guncelleme (/update)
  otaServer.on("/", HTTP_GET, []() {
    otaServer.sendHeader("Connection", "close");
    otaServer.send(200, "text/html", serverIndex);
  });

  otaServer.on("/update", HTTP_POST, []() {
    otaServer.sendHeader("Connection", "close");
    otaServer.send(200, "text/plain", (Update.hasError()) ? "GUNCELLEME BASARISIZ!" : "GUNCELLEME BASARILI! Cihaz yeniden basliyor...");
    delay(1000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = otaServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[Web OTA] Dosya yukleniyor: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[Web OTA] Basariyla yuklendi: %u bayt\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  otaServer.begin();
  _otaInitialized = true;
  Serial.println("[OTA] Kablosuz guncelleme servisi hazir (Arduino IDE portu & http://<IP>/)");
}

void otaTick() {
  if (wifiIsConnected()) {
    ArduinoOTA.handle();
    if (_otaInitialized) {
      otaServer.handleClient();
    }
  }
}

// =============================================================================
// Cloud OTA - Web Panelden Verilen URL ile Uzaktan Indirip Flashlama
// =============================================================================
bool otaPerformCloudUpdate(const char* binUrl) {
  if (!binUrl || strlen(binUrl) < 10) return false;
  if (!wifiIsConnected()) {
    Serial.println("[Cloud OTA] Hata: WiFi baglantisi yok.");
    return false;
  }

  Serial.println("\n[Cloud OTA] ========================================");
  Serial.printf("[Cloud OTA] Uzaktan indirme baslatiliyor: %s\n", binUrl);

  // Ekranda guncelleme durumunu goster
  uiDrawInfoScreen("", "Firmware Indiriliyor...", 0x2F18);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(30000); // 30 saniye zaman asimi

  // GitHub SSL baglantilari ve yonlendirmeler icin dinamik istemci
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // SSL sertifika dogrulamasini esnek tut

  if (!http.begin(secureClient, binUrl)) {
    Serial.println("[Cloud OTA] HTTPS baglanti baslatilamadi!");
    uiDrawErrorScreen("OTA Baglanti Hatasi");
    return false;
  }

  // GitHub User-Agent basligi zorunludur
  http.addHeader("User-Agent", "ESP32-HTTP-OTA");

  const char* headerKeys[] = {"Location", "Content-Length", "Content-Type"};
  http.collectHeaders(headerKeys, 3);

  int httpCode = http.GET();

  // Eger GitHub 302/301 ile AWS/Azure blob depolama adresine yonlendiriyorsa
  if (httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == 307 || httpCode == 308) {
    String redirectUrl = http.header("Location");
    Serial.printf("[Cloud OTA] Yonlendirme tespit edildi -> %s\n", redirectUrl.c_str());
    http.end();

    if (redirectUrl.length() > 0) {
      if (!http.begin(secureClient, redirectUrl)) {
        Serial.println("[Cloud OTA] Yonlendirme adresine baglanilamadi!");
        uiDrawErrorScreen("Yonlendirme Hatasi");
        return false;
      }
      http.addHeader("User-Agent", "ESP32-HTTP-OTA");
      httpCode = http.GET();
    }
  }

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Cloud OTA] HTTP Hatasi: %d\n", httpCode);
    http.end();
    uiDrawErrorScreen("Yazilim Indirilemedi");
    return false;
  }

  int contentLength = http.getSize();
  Serial.printf("[Cloud OTA] Firmware dosya boyutu: %d bayt\n", contentLength);

  if (contentLength <= 0) {
    Serial.println("[Cloud OTA] Gecersiz dosya boyutu!");
    http.end();
    uiDrawErrorScreen("Hatali Dosya Boyutu");
    return false;
  }

  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    Serial.println("[Cloud OTA] Update.begin basarisiz! Flash alani yetersiz.");
    http.end();
    uiDrawErrorScreen("Yetersiz Flash Alani");
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  uint8_t buff[1024];
  int lastProgress = -1;

  while (http.connected() && (written < (size_t)contentLength)) {
    size_t availableBytes = stream->available();
    if (availableBytes) {
      int readBytes = stream->readBytes(buff, ((availableBytes > sizeof(buff)) ? sizeof(buff) : availableBytes));
      written += Update.write(buff, readBytes);

      int progress = (written * 100) / contentLength;
      if (progress != lastProgress && progress % 10 == 0) {
        lastProgress = progress;
        Serial.printf("[Cloud OTA] Ilerleme: %%%d\n", progress);
        char pMsg[32];
        snprintf(pMsg, sizeof(pMsg), "Guncelleniyor: %%%d", progress);
        uiDrawInfoScreen("", pMsg, 0x2F18);
      }
    }
    delay(1);
  }

  http.end();

  if (written != (size_t)contentLength) {
    Serial.printf("[Cloud OTA] Yazma eksik kaldi: %d / %d bayt\n", written, contentLength);
    Update.abort();
    uiDrawErrorScreen("Guncelleme Yarida Kaldi");
    return false;
  }

  if (!Update.end()) {
    Serial.printf("[Cloud OTA] Update.end hatasi #: %d\n", Update.getError());
    uiDrawErrorScreen("Yazilim Dogrulanamadi");
    return false;
  }

  if (!Update.isFinished()) {
    Serial.println("[Cloud OTA] Guncelleme tam olarak sonlanmadi!");
    uiDrawErrorScreen("Bitis Hatasi");
    return false;
  }

  Serial.println("[Cloud OTA] TEBRIKLER! Firmware basariyla yuklendi.");
  uiDrawInfoScreen("", "Yazilim Tamam! Yeniden Baslatiliyor...", 0x07E0);
  delay(1500);
  ESP.restart();
  return true;
}
