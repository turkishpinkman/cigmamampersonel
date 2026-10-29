#pragma once
#include "config.h"

// =============================================================================
// CIGMAMAM PERSONEL SISTEMI - SISTEM KONTROLCUSU BASLIK DOSYASI
// =============================================================================
// Durum makinasi, ekran gecisleri, kart okuma ve dokunmatik mantigi

void systemInit();                              // Sistem kontrolcusunu baslatir
void systemTick();                              // Ana dongu periyodik kontrol mantigi
void systemHandleCard(const char* uid);          // NFC kart okuma islemi
void systemHandleTouch(int16_t x, int16_t y);   // Dokunmatik islemi
ScreenState systemGetScreen();                   // Aktif ekran durumunu dondurur
bool systemIsProcessing();                       // Islem devam ediyor mu bilgisi

// Seri port ve harici yapilandirma yardimcilari
void systemSetWifi(const char* ssid, const char* pass);
void systemSetAdminUID(const char* uid);
void systemSetShift(const ShiftConfig& shift);
ShiftConfig systemGetShift();
void systemPrintStatus();
