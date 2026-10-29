#pragma once

#include "config.h"

// ============================================================================
// WIFI YONETIM MODULU
// Otomatik yeniden baglanma ve durum izleme
// ============================================================================

void wifiInit(const char* ssid, const char* password);
void wifiTick();          // Non-blocking, cagrildiginda baglantiyi kontrol eder
bool wifiIsConnected();   // Baglanti durumu
String wifiGetIP();       // IP adresi string olarak
void wifiUpdateCredentials(const char* ssid, const char* password);  // SSID/sifre guncelle
