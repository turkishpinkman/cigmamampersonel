#pragma once
#include "config.h"

// =============================================================================
// OTA (Over-The-Air) UZAKTAN KOD YUKLEME MODULU
// =============================================================================
// Bu modul sayesinde cihaza USB kablosu takmadan, ayni WiFi agindayken
// Arduino IDE uzerinden kablosuz kod flashlayabilir veya
// http://<Cihaz_IP_Adresi>/update web sayfasindan .bin dosyasi yukleyebilirsiniz.

void otaInit();
void otaTick();
bool otaPerformCloudUpdate(const char* binUrl); // Buluttan (HTTPS/HTTP) firmware indirip yukleme
