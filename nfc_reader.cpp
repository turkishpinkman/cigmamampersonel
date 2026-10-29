#include "nfc_reader.h"
#include <Wire.h>
#include <Adafruit_PN532.h>

// ==========================================
// PN532 I2C instance (IRQ ve RESET baglanmadi)
// ==========================================
static Adafruit_PN532 pn532(-1, -1);  // I2C modu, IRQ/RESET yok
static bool _nfcReady = false;

// Kart varlık / basılı tutma kontrol değişkenleri
static char _presentUID[24] = "";
static bool _cardCurrentlyPresent = false;
static uint8_t _absentCount = 0;

// ==========================================
// nfcInit - PN532'yi baslat ve yapilandir
// ==========================================
bool nfcInit() {
  _nfcReady = false;
  _cardCurrentlyPresent = false;
  _presentUID[0] = '\0';
  _absentCount = 0;
  
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
// nfcRead - Kart okuma (SADECE kart yeni okutuldugunda true doner)
// Kart modul uzerinde bekletildigi surece tekrar true DONMEZ!
// Kartin kaldirilip tekrar yaklastirilmasi gerekir.
// ==========================================
bool nfcRead(char* uidBuf, size_t bufLen) {
  if (!_nfcReady || bufLen < 12) return false;
  
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  
  // 200ms timeout ile kart ara
  bool success = pn532.readPassiveTargetID(
    PN532_MIFARE_ISO14443A, 
    uid, 
    &uidLength, 
    200  // 200ms timeout - kararli ve hizli
  );
  
  if (!success || uidLength == 0) {
    // Kart bulunamadi -> yokluk sayacini artir
    _absentCount++;
    // Ardisik 2 okumada kart yoksa, artik kartin tamamen kaldirildigina emin ol
    if (_absentCount >= 2) {
      if (_cardCurrentlyPresent) {
        Serial.println(F("[NFC] Kart modülden kaldirildi."));
      }
      _cardCurrentlyPresent = false;
      _presentUID[0] = '\0';
      _absentCount = 0;
    }
    return false;
  }
  
  // Kart bulundu -> yokluk sayacini sifirla
  _absentCount = 0;
  
  // UID byte'larini hex string'e donustur
  char currentUID[24];
  char* ptr = currentUID;
  for (uint8_t i = 0; i < uidLength; i++) {
    if (i > 0) {
      *ptr++ = ':';
    }
    sprintf(ptr, "%02X", uid[i]);
    ptr += 2;
  }
  *ptr = '\0';

  // KONTROL: Kart modül üzerinde bekletiliyor mu?
  if (_cardCurrentlyPresent && strcmp(_presentUID, currentUID) == 0) {
    // Ayni kart hala okuyucu uzerinde tutuluyor, tekrar tetikleme!
    return false;
  }
  
  // Yeni bir kart veya kaldirilip tekrar basilan kart!
  _cardCurrentlyPresent = true;
  strncpy(_presentUID, currentUID, sizeof(_presentUID) - 1);
  _presentUID[sizeof(_presentUID) - 1] = '\0';
  
  strncpy(uidBuf, currentUID, bufLen - 1);
  uidBuf[bufLen - 1] = '\0';
  
  Serial.printf("[NFC] Yeni kart basimi: %s (%d byte)\n", uidBuf, uidLength);
  return true;
}

// ==========================================
// nfcResetPresence - Kart algilama durumunu sifirlar
// ==========================================
void nfcResetPresence() {
  _cardCurrentlyPresent = false;
  _presentUID[0] = '\0';
  _absentCount = 0;
}

// ==========================================
// nfcIsCardPresent - Su an bir kart modül üzerinde mi?
// ==========================================
bool nfcIsCardPresent() {
  return _cardCurrentlyPresent;
}

// ==========================================
// nfcIsAvailable - PN532 durumu
// ==========================================
bool nfcIsAvailable() {
  return _nfcReady;
}
