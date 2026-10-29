#pragma once

#include "config.h"

// Zaman yönetimi - NTP + millis() tabanlı (RTC gerektirmez)
void timeInit();               // Baslat, yedekten yukle veya NTP sync
void timeTick();               // Periyodik senkronizasyon ve yedekleme
bool timeSyncNTP();            // Manuel NTP senkronizasyonu
unsigned long timeNow();       // Guncel unix timestamp
void timeFormat(char* buf, unsigned long ts);   // "HH:MM:SS" formatla
void dateFormat(char* buf, unsigned long ts);   // "YYYY-MM-DD" formatla
bool timeIsValid();            // Zaman gecerli mi?
void timeSaveBackup();         // Zamani LittleFS'e yedekle
void timeLoadBackup();         // Yedekten zamani yukle
uint8_t timeGetHour();         // Su anki saat
uint8_t timeGetMinute();       // Su anki dakika
