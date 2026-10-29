#pragma once

#include "config.h"

// PN532 NFC/RFID kart okuyucu (I2C modu)
bool nfcInit();                                    // PN532 baslat
bool nfcRead(char* uidBuf, size_t bufLen);         // Kart oku, sadece YENI basimda true dondurur
bool nfcIsAvailable();                             // PN532 erisilebilir mi?
bool nfcIsCardPresent();                           // Kart su an modul uzerinde mi?
void nfcResetPresence();                           // Kart algilama hafizasini sifirla
