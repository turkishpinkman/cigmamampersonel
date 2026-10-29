#include "touch_ui.h"
#include "config.h"
#include "display_manager.h"
#include <XPT2046_Touchscreen.h>

// Dokunmatik kontrolcu nesnesi
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
static unsigned long _lastTouchTime = 0;
#define TOUCH_DEBOUNCE 80               // Daha kisa debounce (80ms)
#define TOUCH_PRESSURE_MIN 300          // Minimum basinc esigi
#define TOUCH_PRESSURE_MAX 3000         // Maksimum basinc esigi

// Klavye karakter haritasi (4 satir x 7 sutun)
static const char keyboardChars[4][7] = {
    {'A', 'B', 'C', 'D', 'E', 'F', 'G'},
    {'H', 'I', 'J', 'K', 'L', 'M', 'N'},
    {'O', 'P', 'R', 'S', 'T', 'U', 'V'},
    {'Y', 'Z', 'W', 'X', '.', '-', '_'}
};

/**
 * @brief Bir noktanin belirtilen dikdortgen alan icinde olup olmadigini test eder (opsiyonel pay ile).
 */
static inline bool isInRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh, int16_t pad = 2) {
    return (x >= rx - pad && x < rx + rw + pad && y >= ry - pad && y < ry + rh + pad);
}

/**
 * @brief Sayfadaki satir icin gercek personel dizi indeksini dondurur.
 */
static int getEmployeeIndexOnPage(Employee* emps, int totalSlots, int page, int row) {
    if (!emps || page < 0 || row < 0 || row >= 4) return -1;
    int count = 0;
    int target = page * 4 + row;
    for (int i = 0; i < totalSlots; i++) {
        if (emps[i].active) {
            if (count == target) return i;
            count++;
        }
    }
    // Eger active bayragi ayarlanmamissa (fallback), uid kontrol edilir
    if (count == 0) {
        int uidCount = 0;
        for (int i = 0; i < totalSlots; i++) {
            if (emps[i].uid[0] != '\0') {
                if (uidCount == target) return i;
                uidCount++;
            }
        }
        int directIdx = page * 4 + row;
        if (directIdx < totalSlots) return directIdx;
    }
    return -1;
}

/**
 * @brief Dokunmatik kontrolcuyu baslatir.
 */
void touchInit() {
    ts.begin();
    ts.setRotation(1);  // Ekran yatay modu ile eslestir
    Serial.println("[TOUCH] Dokunmatik baslatildi");
}

/**
 * @brief Dokunmatik ekrandan veri okur.
 *        Basinc kontrolu ile gercek dokunmalari filtreler.
 */
bool touchRead(int16_t& x, int16_t& y) {
    // Debounce kontrolu
    if (millis() - _lastTouchTime < TOUCH_DEBOUNCE) {
        return false;
    }

    // Dokunma var mi kontrol et
    if (!ts.touched()) {
        return false;
    }

    TS_Point p = ts.getPoint();

    // Basinc filtresi - hayalet dokunmalari ele
    if (p.z < TOUCH_PRESSURE_MIN || p.z > TOUCH_PRESSURE_MAX) {
        return false;
    }

    _lastTouchTime = millis();

    // Ham dokunma koordinatlarini ekran piksellerine donustur
    // Her iki eksen ters (donanim baglantisina gore)
    long mx = map(p.x, TS_MINX, TS_MAXX, SCREEN_W, 0);
    long my = map(p.y, TS_MINY, TS_MAXY, SCREEN_H, 0);

    // Ekran sinirlari icinde tut
    if (mx < 0) mx = 0;
    if (mx >= SCREEN_W) mx = SCREEN_W - 1;
    if (my < 0) my = 0;
    if (my >= SCREEN_H) my = SCREEN_H - 1;

    x = (int16_t)mx;
    y = (int16_t)my;

    Serial.printf("[TOUCH] HAM: x=%d y=%d z=%d -> EKRAN: x=%d y=%d\n", p.x, p.y, p.z, x, y);

    return true;
}

/**
 * @brief Ana bekleme ekranini cizer (saat, kart okutun uyarisi, durum bilgisi).
 */
// Ana ekran partial update icin static degiskenler
static bool _mainScreenDrawn = false;
static char _lastTimeStr[12] = "";
static int _lastTotalEmp = -1;
static int _lastActiveEmp = -1;
static bool _lastWifiOn = false;

void uiDrawMainScreen(const char* timeStr, int totalEmp, int activeEmp, bool wifiOn) {
    bool needFullDraw = !_mainScreenDrawn;

    if (needFullDraw) {
        displayClear(COLOR_BG);
        displayHeader("CIGMAMAM PERSONEL", COLOR_HEADER_BG);

        // Kucuk bilgilendirme paneli (kutu)
        tft.drawRoundRect(15, 38, 290, 85, 8, COLOR_LIGHT_BLUE);
        tft.fillRoundRect(17, 40, 286, 81, 6, 0x0842); // Koyu lacivert/gri derinlik

        // Alt Talimat metni
        displayCentered(135, "Lutfen Kart Okutun", COLOR_WARNING, 2);

        // Buton 1: SYNC (Bulut Senkronizasyonu) Sol Alt
        displayButton(15, 175, 135, 36, "SYNC", 0x03E0, COLOR_TEXT, 2);

        // Buton 2: + EKLE (Yeni Personel Kaydi) Sag Alt
        displayButton(170, 175, 135, 36, "+ EKLE", 0x001F, COLOR_TEXT, 2);

        _mainScreenDrawn = true;
    }

    // Saat guncelleme (sadece degistiyse)
    const char* tStr = (timeStr && timeStr[0]) ? timeStr : "--:--:--";
    if (needFullDraw || strcmp(tStr, _lastTimeStr) != 0) {
        // Saat alanini temizle ve buyuk punto ile yaz
        tft.fillRect(25, 48, 270, 32, 0x0842);
        displayCentered(52, tStr, COLOR_TEXT, 3);
        strncpy(_lastTimeStr, tStr, sizeof(_lastTimeStr) - 1);
    }

    // Personel sayisi guncelleme (sadece degistiyse)
    if (needFullDraw || totalEmp != _lastTotalEmp || activeEmp != _lastActiveEmp) {
        tft.fillRect(25, 90, 270, 20, 0x0842);
        char empBuf[40];
        snprintf(empBuf, sizeof(empBuf), "Kayitli: %d   Iceride: %d", totalEmp, activeEmp);
        displayCentered(94, empBuf, COLOR_TEXT, 1);
        _lastTotalEmp = totalEmp;
        _lastActiveEmp = activeEmp;
    }

    // Status bar guncelleme (sadece wifi degistiyse)
    if (needFullDraw || wifiOn != _lastWifiOn) {
        displayStatusBar(tStr, wifiOn);
        _lastWifiOn = wifiOn;
    }
}

/**
 * @brief Ana ekran tam yeniden cizimi icin flag'i sifirlar.
 *        Baska bir ekrandan ana ekrana donuldiginde cagrilmalidir.
 */
void uiResetMainScreen() {
    _mainScreenDrawn = false;
    _lastTimeStr[0] = '\0';
    _lastTotalEmp = -1;
    _lastActiveEmp = -1;
    _lastWifiOn = false;
}

/**
 * @brief Personel icin MOLA / CIKIS secim ekranini cizer.
 */
void uiDrawSelectionScreen(const char* name) {
    displayClear(COLOR_BG);
    displayHeader(name ? name : "PERSONEL", COLOR_SUCCESS);

    displayCentered(50, "Ne yapmak", COLOR_TEXT, 2);
    displayCentered(75, "istiyorsunuz?", COLOR_TEXT, 2);

    // MOLA ve CIKIS butonlari
    displayButton(10, 110, 145, 100, "MOLA", COLOR_MOLA_BTN, COLOR_TEXT, 3);
    displayButton(165, 110, 145, 100, "CIKIS", COLOR_CIKIS_BTN, COLOR_TEXT, 3);
}

/**
 * @brief Secim ekrani dokunma kontrolu (0=yok, 1=mola, 2=cikis).
 */
int uiCheckSelectionTouch(int16_t x, int16_t y) {
    if (isInRect(x, y, 10, 110, 145, 100, 4)) {
        return 1;  // MOLA
    }
    if (isInRect(x, y, 165, 110, 145, 100, 4)) {
        return 2;  // CIKIS
    }
    return 0;
}

/**
 * @brief Bilgi/Onay bildirim ekranini cizer.
 */
void uiDrawInfoScreen(const char* name, const char* msg, uint16_t color) {
    displayClear(COLOR_BG);
    displayHeader(name ? name : "", color);

    // Ortada renkli durum dairesi ve ikonu
    int16_t cx = SCREEN_W / 2;
    int16_t cy = 95;
    int16_t radius = 28;
    tft.fillCircle(cx, cy, radius, color);
    tft.drawCircle(cx, cy, radius, COLOR_TEXT);

    if (color == COLOR_SUCCESS) {
        // Tik (Onay) isareti
        tft.drawLine(cx - 14, cy, cx - 4, cy + 12, COLOR_TEXT);
        tft.drawLine(cx - 13, cy, cx - 3, cy + 12, COLOR_TEXT);
        tft.drawLine(cx - 4, cy + 12, cx + 14, cy - 10, COLOR_TEXT);
        tft.drawLine(cx - 3, cy + 12, cx + 15, cy - 10, COLOR_TEXT);
    } else if (color == COLOR_ERROR || color == COLOR_CIKIS_BTN) {
        // Carpi (X) isareti
        tft.drawLine(cx - 12, cy - 12, cx + 12, cy + 12, COLOR_TEXT);
        tft.drawLine(cx - 11, cy - 12, cx + 13, cy + 12, COLOR_TEXT);
        tft.drawLine(cx + 12, cy - 12, cx - 12, cy + 12, COLOR_TEXT);
        tft.drawLine(cx + 11, cy - 12, cx - 13, cy + 12, COLOR_TEXT);
    } else {
        // Duraklatma / Uyari sembolu
        tft.fillRect(cx - 8, cy - 12, 6, 24, COLOR_TEXT);
        tft.fillRect(cx + 2, cy - 12, 6, 24, COLOR_TEXT);
    }

    if (msg) {
        displayCentered(150, msg, COLOR_TEXT, 2);
    }
}

/**
 * @brief Hata ekranini cizer.
 */
void uiDrawErrorScreen(const char* msg) {
    displayClear(COLOR_BG);
    displayHeader("HATA", COLOR_ERROR);

    // Kirmizi hata dairesi ve X
    int16_t cx = SCREEN_W / 2;
    int16_t cy = 90;
    int16_t radius = 28;
    tft.fillCircle(cx, cy, radius, COLOR_ERROR);
    tft.drawCircle(cx, cy, radius, COLOR_TEXT);

    tft.drawLine(cx - 12, cy - 12, cx + 12, cy + 12, COLOR_TEXT);
    tft.drawLine(cx - 11, cy - 12, cx + 13, cy + 12, COLOR_TEXT);
    tft.drawLine(cx + 12, cy - 12, cx - 12, cy + 12, COLOR_TEXT);
    tft.drawLine(cx + 11, cy - 12, cx - 13, cy + 12, COLOR_TEXT);

    if (msg) {
        displayCentered(150, msg, COLOR_TEXT, 2);
    }
}

/**
 * @brief Yonetici ana menusunu cizer.
 */
void uiDrawAdminMenu() {
    displayClear(COLOR_BG);
    displayHeader("YONETICI PANELI", COLOR_HEADER_BG);

    const char* menuLabels[5] = {
        "+ Personel Ekle",
        "- Personel Sil",
        "Mesai Ayarla",
        "Personel Liste",
        "Geri"
    };

    for (int i = 0; i < 5; i++) {
        int16_t by = 40 + i * 40;
        uint16_t btnBg = (i == 4) ? COLOR_DARK_GRAY : COLOR_ADMIN_BTN;
        displayButton(20, by, 280, 34, menuLabels[i], btnBg, COLOR_TEXT, 2);
    }
}

/**
 * @brief Yonetici menusu dokunma kontrolu (0-4 menu indeksi, -1=yok).
 */
int uiCheckAdminTouch(int16_t x, int16_t y) {
    for (int i = 0; i < 5; i++) {
        int16_t by = 40 + i * 40;
        if (isInRect(x, y, 20, by, 280, 34, 3)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Personel listesi ekranini cizer (sayfa basina 4 kayit).
 */
void uiDrawEmployeeList(Employee* emps, int totalSlots, int page) {
    displayClear(COLOR_BG);
    displayHeader("PERSONEL LISTESI", COLOR_HEADER_BG);

    for (int r = 0; r < 4; r++) {
        int idx = getEmployeeIndexOnPage(emps, totalSlots, page, r);
        if (idx >= 0) {
            int16_t rowY = 36 + r * 38;
            displayFillRect(10, rowY, 300, 34, COLOR_CARD_BG);
            displayRect(10, rowY, 300, 34, COLOR_DARK_GRAY);

            // Durum rengi ve metni
            uint16_t stColor = COLOR_GRAY;
            const char* stText = "DISARIDA";
            if (emps[idx].state == STATE_CALISIYOR) {
                stColor = COLOR_SUCCESS;
                stText = "CALISIYOR";
            } else if (emps[idx].state == STATE_MOLADA) {
                stColor = COLOR_MOLA_BTN;
                stText = "MOLADA";
            }

            // Durum noktasi
            tft.fillCircle(24, rowY + 17, 5, stColor);

            // Isim ve UID bilgisi
            displayText(36, rowY + 5, emps[idx].name, COLOR_TEXT, 1);
            displayText(36, rowY + 19, emps[idx].uid, COLOR_GRAY, 1);

            // Durum metni
            displayText(SCREEN_W - 80, rowY + 13, stText, stColor, 1);
        }
    }

    // Gezinme butonlari
    displayButton(10, 195, 90, 35, "< Onceki", COLOR_DARK_GRAY, COLOR_TEXT, 1);
    displayButton(115, 195, 90, 35, "Geri", COLOR_ADMIN_BTN, COLOR_TEXT, 1);
    displayButton(220, 195, 90, 35, "Sonraki >", COLOR_DARK_GRAY, COLOR_TEXT, 1);
}

/**
 * @brief Personel listesi dokunma kontrolu (emp index, -2=prev, -3=next, -4=back, -1=yok).
 */
int uiCheckListTouch(int16_t x, int16_t y, int page, int totalSlots, Employee* emps) {
    // Onceki sayfa butonu
    if (isInRect(x, y, 10, 195, 90, 35, 3)) {
        return -2;
    }
    // Geri butonu
    if (isInRect(x, y, 115, 195, 90, 35, 3)) {
        return -4;
    }
    // Sonraki sayfa butonu
    if (isInRect(x, y, 220, 195, 90, 35, 3)) {
        return -3;
    }

    // Personel satirlari (0..3)
    for (int r = 0; r < 4; r++) {
        int16_t rowY = 36 + r * 38;
        if (isInRect(x, y, 10, rowY, 300, 34, 2)) {
            int empIdx = getEmployeeIndexOnPage(emps, totalSlots, page, r);
            if (empIdx >= 0) {
                return empIdx;
            }
        }
    }

    return -1;
}

/**
 * @brief Yeni personel ekleme ekranini cizer (kart UID'si gosterilir).
 */
void uiDrawAddEmployeeScreen(const char* uid) {
    displayClear(COLOR_BG);
    displayHeader("PERSONEL EKLE", COLOR_HEADER_BG);

    // Kart UID kutusu
    displayFillRect(20, 50, 280, 50, COLOR_DARK_GRAY);
    displayRect(20, 50, 280, 50, COLOR_LIGHT_BLUE);
    displayText(30, 58, "Kart UID:", COLOR_WARNING, 1);
    displayText(30, 74, (uid && uid[0]) ? uid : "---", COLOR_TEXT, 2);

    // Yonlendirme metni
    displayCentered(118, "Isim girmek icin", COLOR_TEXT, 2);
    displayCentered(142, "EKLE butonuna basin", COLOR_TEXT, 1);

    // Islem butonlari
    displayButton(30, 172, 120, 45, "EKLE", COLOR_SUCCESS, COLOR_TEXT, 2);
    displayButton(170, 172, 120, 45, "GERI", COLOR_ERROR, COLOR_TEXT, 2);
}

/**
 * @brief Personel ekleme ekrani dokunma kontrolu (1=ekle, 2=geri, 0=yok).
 */
int uiCheckAddEmployeeTouch(int16_t x, int16_t y) {
    if (isInRect(x, y, 30, 172, 120, 45, 4)) {
        return 1;  // EKLE
    }
    if (isInRect(x, y, 170, 172, 120, 45, 4)) {
        return 2;  // GERI
    }
    return 0;
}

/**
 * @brief Sanal klavye ekranini cizer.
 */
void uiDrawKeyboard(const char* currentText) {
    displayClear(COLOR_BG);

    // Metin giris alani (ust bar)
    displayFillRect(10, 5, 300, 34, COLOR_DARK_GRAY);
    displayRect(10, 5, 300, 34, COLOR_LIGHT_BLUE);

    char dispBuf[48];
    snprintf(dispBuf, sizeof(dispBuf), "%s_", (currentText ? currentText : ""));
    if (strlen(dispBuf) > 16) {
        displayText(16, 17, dispBuf, COLOR_TEXT, 1);
    } else {
        displayText(16, 14, dispBuf, COLOR_TEXT, 2);
    }

    // Tuslar (4 satir x 7 sutun)
    for (int r = 0; r < 4; r++) {
        int16_t ky = 44 + r * 32;
        for (int c = 0; c < 7; c++) {
            int16_t kx = 9 + c * 44;
            char kLabel[2] = { keyboardChars[r][c], '\0' };
            displayButton(kx, ky, 38, 28, kLabel, COLOR_DARK_GRAY, COLOR_TEXT, 2);
        }
    }

    // Alt islem butonlari
    displayButton(10, 176, 90, 42, "BOSLUK", COLOR_CARD_BG, COLOR_TEXT, 1);
    displayButton(115, 176, 90, 42, "SIL", COLOR_MOLA_BTN, COLOR_TEXT, 2);
    displayButton(220, 176, 90, 42, "TAMAM", COLOR_SUCCESS, COLOR_TEXT, 2);
}

/**
 * @brief Klavye ekrani dokunma kontrolu (karakter, '\b'=sil, '\n'=tamam, 0=yok).
 */
char uiCheckKeyboardTouch(int16_t x, int16_t y) {
    // Alt islem butonlari kontrolu
    if (isInRect(x, y, 10, 176, 90, 42, 3)) {
        return ' ';   // Bosluk
    }
    if (isInRect(x, y, 115, 176, 90, 42, 3)) {
        return '\b';  // Backspace / Sil
    }
    if (isInRect(x, y, 220, 176, 90, 42, 3)) {
        return '\n';  // Tamam / Onay
    }

    // Karakter tuslari matrisi kontrolu
    for (int r = 0; r < 4; r++) {
        int16_t ky = 44 + r * 32;
        if (y >= ky - 3 && y < ky + 28 + 3) {
            for (int c = 0; c < 7; c++) {
                int16_t kx = 9 + c * 44;
                if (x >= kx - 3 && x < kx + 38 + 3) {
                    return keyboardChars[r][c];
                }
            }
        }
    }

    return 0;
}

/**
 * @brief Mesai saatleri ayar ekranini cizer.
 */
void uiDrawShiftSettings(ShiftConfig& cfg) {
    displayClear(COLOR_BG);
    displayHeader("MESAI AYARLARI", COLOR_HEADER_BG);

    // Baslangic saati bolumu
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "Baslangic:  %02d:%02d", cfg.startHour, cfg.startMinute);
    displayText(20, 38, timeBuf, COLOR_TEXT, 2);

    displayButton(20, 62, 60, 30, "+H", COLOR_DARK_GRAY, COLOR_TEXT, 2);
    displayButton(90, 62, 60, 30, "-H", COLOR_DARK_GRAY, COLOR_TEXT, 2);
    displayButton(170, 62, 60, 30, "+M", COLOR_DARK_GRAY, COLOR_TEXT, 2);
    displayButton(240, 62, 60, 30, "-M", COLOR_DARK_GRAY, COLOR_TEXT, 2);

    // Bitis saati bolumu
    snprintf(timeBuf, sizeof(timeBuf), "Bitis:      %02d:%02d", cfg.endHour, cfg.endMinute);
    displayText(20, 104, timeBuf, COLOR_TEXT, 2);

    displayButton(20, 128, 60, 30, "+H", COLOR_DARK_GRAY, COLOR_TEXT, 2);
    displayButton(90, 128, 60, 30, "-H", COLOR_DARK_GRAY, COLOR_TEXT, 2);
    displayButton(170, 128, 60, 30, "+M", COLOR_DARK_GRAY, COLOR_TEXT, 2);
    displayButton(240, 128, 60, 30, "-M", COLOR_DARK_GRAY, COLOR_TEXT, 2);

    // Kaydet ve Geri butonlari
    displayButton(25, 178, 125, 42, "KAYDET", COLOR_SUCCESS, COLOR_TEXT, 2);
    displayButton(170, 178, 125, 42, "GERI", COLOR_ERROR, COLOR_TEXT, 2);
}

/**
 * @brief Mesai ayari dokunma kontrolu (0-7=saat/dk butonlari, 8=kaydet, 9=geri, -1=yok).
 */
int uiCheckShiftTouch(int16_t x, int16_t y) {
    // Baslangic saati butonlari (y = 62 civari)
    if (isInRect(x, y, 20, 62, 60, 30, 4)) return 0;   // Baslangic Saat +
    if (isInRect(x, y, 90, 62, 60, 30, 4)) return 1;   // Baslangic Saat -
    if (isInRect(x, y, 170, 62, 60, 30, 4)) return 2;  // Baslangic Dakika +
    if (isInRect(x, y, 240, 62, 60, 30, 4)) return 3;  // Baslangic Dakika -

    // Bitis saati butonlari (y = 128 civari)
    if (isInRect(x, y, 20, 128, 60, 30, 4)) return 4;  // Bitis Saat +
    if (isInRect(x, y, 90, 128, 60, 30, 4)) return 5;  // Bitis Saat -
    if (isInRect(x, y, 170, 128, 60, 30, 4)) return 6;  // Bitis Dakika +
    if (isInRect(x, y, 240, 128, 60, 30, 4)) return 7;  // Bitis Dakika -

    // Kaydet ve Geri butonlari (y = 178 civari)
    if (isInRect(x, y, 25, 178, 125, 42, 4)) return 8;  // Kaydet
    if (isInRect(x, y, 170, 178, 125, 42, 4)) return 9; // Geri

    return -1;
}

/**
 * @brief Personel silme onay ekranini cizer.
 */
void uiDrawDeleteConfirm(const char* name) {
    displayClear(COLOR_BG);
    displayHeader("PERSONEL SIL", COLOR_ERROR);

    displayCentered(70, (name && name[0]) ? name : "Personel", COLOR_WARNING, 2);
    displayCentered(105, "silinsin mi?", COLOR_TEXT, 2);

    displayButton(30, 150, 120, 55, "EVET", COLOR_SUCCESS, COLOR_TEXT, 2);
    displayButton(170, 150, 120, 55, "HAYIR", COLOR_ERROR, COLOR_TEXT, 2);
}

/**
 * @brief Personel silme onay ekrani dokunma kontrolu (1=evet, 2=hayir, 0=yok).
 */
int uiCheckDeleteTouch(int16_t x, int16_t y) {
    if (isInRect(x, y, 30, 150, 120, 55, 4)) {
        return 1;  // EVET
    }
    if (isInRect(x, y, 170, 150, 120, 55, 4)) {
        return 2;  // HAYIR
    }
    return 0;
}


// ==========================================
// HIZLI PERSONEL KAYIT EKRANI
// ==========================================

/**
 * @brief "Personel Ekle" kayit ekranini cizer.
 *        Kullanici kartini okutarak hizli kayit yapar.
 */
void uiDrawRegisterCardScreen() {
    displayClear(COLOR_BG);
    displayHeader("YENI PERSONEL KAYDI", COLOR_SUCCESS);

    // NFC kart ikonu (buyuk)
    int16_t cardW = 80, cardH = 50;
    int16_t cardX = (SCREEN_W - cardW) / 2;
    int16_t cardY = 55;
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 6, COLOR_LIGHT_BLUE);
    tft.fillRoundRect(cardX + 3, cardY + 3, cardW - 6, cardH - 6, 4, COLOR_CARD_BG);
    tft.drawRoundRect(cardX + 14, cardY + 14, 20, 20, 3, COLOR_WARNING);
    tft.drawLine(cardX + 24, cardY + 14, cardX + 24, cardY + 34, COLOR_WARNING);
    tft.drawLine(cardX + 14, cardY + 24, cardX + 34, cardY + 24, COLOR_WARNING);

    // Talimat
    displayCentered(120, "Yeni personelin", COLOR_TEXT, 2);
    displayCentered(145, "kartini okutun", COLOR_TEXT, 2);

    // Iptal butonu
    displayButton(90, 185, 140, 35, "IPTAL", COLOR_ERROR, COLOR_TEXT, 2);
}

/**
 * @brief Ana ekrandaki "+ EKLE" butonuna dokunulup dokunulmadigini kontrol eder (170, 175, 135x36).
 */
bool uiCheckRegisterButtonTouch(int16_t x, int16_t y) {
    return isInRect(x, y, 170, 175, 135, 36, 6);
}

/**
 * @brief Ana ekrandaki "SYNC" butonuna dokunulup dokunulmadigini kontrol eder (15, 175, 135x36).
 */
bool uiCheckSyncButtonTouch(int16_t x, int16_t y) {
    return isInRect(x, y, 15, 175, 135, 36, 6);
}
