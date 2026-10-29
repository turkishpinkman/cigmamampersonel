#pragma once

#include <Arduino.h>
#include "config.h"

// LittleFS'den olaylari yukler
void eventInit();

// Yeni olay kaydeder
void eventLog(const char* uid, const char* name, EventType type, unsigned long timestamp);

// Senkronize edilmemis olay sayisini dondurur
int eventPendingCount();

// Bekleyen olaylari sorgulamak icin dizi pointer'i dondurur (count = toplam olay sayisi)
LogEvent* eventGetPending(int& count);

// Belirtilen ID'ye sahip olayi senkronize edilmis olarak isaretler
void eventMarkSynced(unsigned long id);

// Eski senkronize edilmis olaylari temizler (7 gun ve daha eski olanlar)
void eventCleanup();

// Olay listesini LittleFS'e kaydeder
void eventSave();

// Toplam olay sayisini dondurur
int eventTotalCount();
