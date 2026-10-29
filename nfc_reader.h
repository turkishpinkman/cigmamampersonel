#pragma once

#include "config.h"

// PN532 NFC/RFID kart okuyucu (I2C modu)
bool nfcInit();                                    // PN532 baslat
bool nfcRead(char* uidBuf, size_t bufLen);         // Kart oku, UID string dondur
bool nfcIsAvailable();                             // PN532 erisilebilir mi?
