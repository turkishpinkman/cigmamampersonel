#include "storage_manager.h"
#include "config.h"
#include <LittleFS.h>

bool storageInit() {
  Serial.println("[Storage] LittleFS baslatiliyor...");

  // true: Baslatma basarisiz olursa bicimlendir (format on fail)
  if (!LittleFS.begin(true)) {
    Serial.println("[Storage] HATA: LittleFS baslatilamadi!");
    return false;
  }

  Serial.println("[Storage] LittleFS basariyla baslatildi.");
  Serial.print("[Storage] Toplam Alan: ");
  Serial.print(LittleFS.totalBytes());
  Serial.print(" byte | Kullanilan: ");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" byte | Bos: ");
  Serial.print(storageGetFreeSpace());
  Serial.println(" byte");

  return true;
}

String storageRead(const char* path) {
  if (path == nullptr || strlen(path) == 0) {
    Serial.println("[Storage] HATA: Gecersiz dosya yolu!");
    return String("");
  }

  if (!LittleFS.exists(path)) {
    Serial.print("[Storage] Dosya bulunamadi: ");
    Serial.println(path);
    return String("");
  }

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.print("[Storage] HATA: Dosya okuma icin acilamadi: ");
    Serial.println(path);
    return String("");
  }

  String content = file.readString();
  file.close();

  return content;
}

bool storageWrite(const char* path, const String& data) {
  if (path == nullptr || strlen(path) == 0) {
    Serial.println("[Storage] HATA: Gecersiz dosya yolu!");
    return false;
  }

  // ATOMIK YAZMA DESENI:
  // 1. Gecici dosyaya (.tmp) yaz
  // 2. Basarili olursa hedef dosya varsa sil
  // 3. Gecici dosyayi asil dosya adina tasi
  // Boylece ani guc kesintilerinde veri bozulmasi engellenir.

  String tmpPath = String(path) + ".tmp";

  File tmpFile = LittleFS.open(tmpPath.c_str(), "w");
  if (!tmpFile) {
    Serial.print("[Storage] HATA: Gecici dosya olusturulamadi: ");
    Serial.println(tmpPath);
    return false;
  }

  size_t bytesWritten = tmpFile.print(data);
  tmpFile.flush();
  tmpFile.close();

  if (bytesWritten != data.length()) {
    Serial.println("[Storage] HATA: Veri gecici dosyaya eksik yazildi!");
    LittleFS.remove(tmpPath.c_str());
    return false;
  }

  // Eski dosya varsa kaldir
  if (LittleFS.exists(path)) {
    if (!LittleFS.remove(path)) {
      Serial.print("[Storage] UYARI: Eski dosya silinemedi: ");
      Serial.println(path);
    }
  }

  // Gecici dosyayi asil dosya ismiyle yeniden adlandir
  if (!LittleFS.rename(tmpPath.c_str(), path)) {
    Serial.print("[Storage] HATA: Dosya yeniden adlandirilamadi: ");
    Serial.println(path);
    LittleFS.remove(tmpPath.c_str());
    return false;
  }

  Serial.print("[Storage] Dosya basariyla yazildi: ");
  Serial.print(path);
  Serial.print(" (");
  Serial.print(bytesWritten);
  Serial.println(" byte)");

  return true;
}

bool storageAppend(const char* path, const String& data) {
  if (path == nullptr || strlen(path) == 0) {
    Serial.println("[Storage] HATA: Gecersiz dosya yolu!");
    return false;
  }

  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.print("[Storage] HATA: Ekleme icin dosya acilamadi: ");
    Serial.println(path);
    return false;
  }

  size_t bytesWritten = file.print(data);
  file.flush();
  file.close();

  if (bytesWritten != data.length()) {
    Serial.println("[Storage] HATA: Veri dosyaya eksik eklendi!");
    return false;
  }

  return true;
}

bool storageDelete(const char* path) {
  if (path == nullptr || strlen(path) == 0) {
    Serial.println("[Storage] HATA: Gecersiz dosya yolu!");
    return false;
  }

  if (!LittleFS.exists(path)) {
    Serial.print("[Storage] Silinecek dosya mevcut degil: ");
    Serial.println(path);
    return false;
  }

  bool removed = LittleFS.remove(path);
  if (removed) {
    Serial.print("[Storage] Dosya silindi: ");
    Serial.println(path);
  } else {
    Serial.print("[Storage] HATA: Dosya silinemedi: ");
    Serial.println(path);
  }

  return removed;
}

bool storageExists(const char* path) {
  if (path == nullptr || strlen(path) == 0) {
    return false;
  }
  return LittleFS.exists(path);
}

unsigned long storageGetFreeSpace() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();

  if (total >= used) {
    return (unsigned long)(total - used);
  }
  return 0;
}
