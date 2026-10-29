#include "nfc_reader.h"
#include <Wire.h>
#include <Adafruit_PN532.h>

// ==========================================
// PN532 I2C instance (IRQ ve RESET baglanmadi)
// ==========================================
static Adafruit_PN532 pn532(-1, -1);  // I2C modu, IRQ/RESET yok
static bool _nfcReady = false;

// ==========================================
// nfcInit - PN532'yi baslat ve yapilandir
// ==========================================
bool nfcInit() {
  _nfcReady = false;
  
  // I2C baslatma (ozel pinler)
  Wire.begin(NFC_SDA, NFC_SCL);
  Wire.setClock(100000);  // 100kHz - kararlı PN532 iletisimi icin sart!
  
  pn532.begin();
  
  // Firmware versiyonunu kontrol et
  uint32_t versiondata = pn532.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("[NFC] PN532 bulunamadi! Kablo baglantilarini kontrol edin.");
    Serial.println("[NFC]   SDA -> GPIO21, SCL -> GPIO22");
    Serial.println("[NFC]   I2C modu jumper'i kontrol edin");
    return false;
  }
  
  // Firmware bilgisini yazdir
  Serial.printf("[NFC] PN532 bulundu - Chip: PN5%02X, Firmware: %d.%d\n",
                (versiondata >> 24) & 0xFF,
                (versiondata >> 16) & 0xFF,
                (versiondata >> 8) & 0xFF);
  
  // RF alanini ve kart okuma parametrelerini yapilandir
  pn532.SAMConfig();
  
  _nfcReady = true;
  Serial.println("[NFC] PN532 baslatildi ve hazir");
  
  return true;
}

// ==========================================
// nfcRead - Kart okuma (100ms timeout)
// ==========================================
bool nfcRead(char* uidBuf, size_t bufLen) {
  if (!_nfcReady || bufLen < 12) return false;
  
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  
  // 500ms timeout ile kart ara (test kodundaki calisir deger)
  bool success = pn532.readPassiveTargetID(
    PN532_MIFARE_ISO14443A, 
    uid, 
    &uidLength, 
    500  // 500ms timeout - kararli okuma icin gerekli
  );
  
  if (!success || uidLength == 0) {
    return false;
  }
  
  // UID byte'larini hex string'e donustur
  // Ornek: 4 byte -> "AA:BB:CC:DD" (11 karakter + null)
  // Ornek: 7 byte -> "AA:BB:CC:DD:EE:FF:GG" (20 karakter + null)
  
  size_t needed = uidLength * 3;  // "XX:" formatinda her byte 3 karakter
  if (needed > bufLen) {
    Serial.println("[NFC] UID buffer yetersiz!");
    return false;
  }
  
  char* ptr = uidBuf;
  for (uint8_t i = 0; i < uidLength; i++) {
    if (i > 0) {
      *ptr++ = ':';
    }
    sprintf(ptr, "%02X", uid[i]);
    ptr += 2;
  }
  *ptr = '\0';
  
  Serial.printf("[NFC] Kart okundu: %s (%d byte)\n", uidBuf, uidLength);
  
  return true;
}

// ==========================================
// nfcIsAvailable - PN532 durumu
// ==========================================
bool nfcIsAvailable() {
  return _nfcReady;
}
