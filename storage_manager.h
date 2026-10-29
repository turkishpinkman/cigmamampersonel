#pragma once

#include "config.h"

// ============================================================================
// LITTLEFS DEPOLAMA YONETICISI
// Guvenli ve atomik dosya islemleri sarmalayicisi
// ============================================================================

bool storageInit();                              // LittleFS baslat
String storageRead(const char* path);            // Dosya oku, hata durumunda bos string
bool storageWrite(const char* path, const String& data);  // Atomic yazma
bool storageAppend(const char* path, const String& data); // Dosyaya ekle
bool storageDelete(const char* path);            // Dosya sil
bool storageExists(const char* path);            // Dosya var mi?
unsigned long storageGetFreeSpace();             // Bos alan (byte)
