#include "touch_ui.h"
#include "config.h"
#include "display_manager.h"
#include <XPT2046_Touchscreen.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

// Yeni arayuz paleti: koyu lacivert, turkuaz ve amber.
#define UI_BG       0x0864
#define UI_PANEL    0x10E7
#define UI_PANEL_2  0x1949
#define UI_TEAL     0x2F18
#define UI_AMBER    0xFDA8
#define UI_RED      0xFAEE
#define UI_MUTED    0x8D18
#define UI_WHITE    0xFFFF

static void drawTextFont(int16_t x, int16_t baseline, const char* text, uint16_t color,
                         const GFXfont* font = &FreeSansBold12pt7b) {
    if (!text) return;
    tft.setTextSize(1);
    tft.setFont(font);
    tft.setTextColor(color);
    tft.setCursor(x, baseline);
    tft.print(text);
    tft.setFont();
}

static void drawCenteredFont(int16_t baseline, const char* text, uint16_t color,
                             const GFXfont* font = &FreeSansBold12pt7b) {
    if (!text) return;
    tft.setTextSize(1);
    tft.setFont(font);
    int16_t x1, y1;
    uint16_t textW, textH;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &textW, &textH);
    int16_t x = (SCREEN_W - (int16_t)textW) / 2 - x1;
    tft.setTextColor(color);
    tft.setCursor(x, baseline);
    tft.print(text);
    tft.setFont();
}

static void drawCenteredInRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               const char* text, uint16_t color,
                               const GFXfont* font = &FreeSansBold12pt7b) {
    if (!text || !text[0]) return;
    tft.setTextSize(1);
    tft.setFont(font);
    int16_t x1, y1;
    uint16_t textW, textH;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &textW, &textH);
    int16_t tx = x + (w - (int16_t)textW) / 2 - x1;
    int16_t baseline = y + (h - (int16_t)textH) / 2 - y1;
    tft.setTextColor(color);
    tft.setCursor(tx, baseline);
    tft.print(text);
    tft.setFont();
}

static void drawSmallText(int16_t x, int16_t y, const char* text, uint16_t color) {
    if (!text) return;
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(text);
}

static void drawScreenHeader(const char* title, uint16_t accent = UI_TEAL) {
    tft.fillRect(0, 0, SCREEN_W, 32, UI_PANEL);
    tft.fillRect(0, 0, 5, 32, accent);
    drawTextFont(14, 23, title ? title : "", UI_WHITE);
    tft.drawFastHLine(14, 31, SCREEN_W - 28, UI_PANEL_2);
}

static void drawUiButton(int16_t x, int16_t y, int16_t w, int16_t h,
                         const char* label, uint16_t fill, uint16_t accent,
                         const GFXfont* font = &FreeSansBold12pt7b) {
    tft.fillRoundRect(x, y + 3, w, h, 8, 0x0000);
    tft.fillRoundRect(x, y, w, h, 8, fill);
    tft.drawRoundRect(x, y, w, h, 8, accent);
    tft.fillRoundRect(x + 3, y + 5, 3, h - 10, 1, accent);
    drawCenteredInRect(x + 8, y + 1, w - 12, h - 2, label, UI_WHITE, font);
}

static void drawNfcIcon(int16_t cx, int16_t cy, uint16_t color, uint8_t scale = 1) {
    int16_t w = 24 * scale, h = 18 * scale;
    tft.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 3 * scale, color);
    tft.drawFastHLine(cx - w / 2 + 4 * scale, cy - 2 * scale, 9 * scale, color);
    tft.drawFastHLine(cx - w / 2 + 4 * scale, cy + 3 * scale, 6 * scale, color);
    tft.drawCircle(cx + w / 2 + 5 * scale, cy, 3 * scale, color);
    tft.drawCircle(cx + w / 2 + 10 * scale, cy, 6 * scale, color);
}

static void drawPersonIcon(int16_t cx, int16_t cy, uint16_t color) {
    tft.drawCircle(cx, cy - 5, 4, color);
    tft.drawRoundRect(cx - 9, cy + 1, 18, 12, 5, color);
}

static void drawWifiMark(int16_t cx, int16_t cy, bool connected) {
    uint16_t color = connected ? UI_TEAL : UI_RED;
    tft.drawLine(cx - 7, cy - 3, cx, cy + 2, color);
    tft.drawLine(cx, cy + 2, cx + 7, cy - 3, color);
    tft.drawLine(cx - 4, cy, cx, cy + 3, color);
    tft.drawLine(cx, cy + 3, cx + 4, cy, color);
    tft.fillCircle(cx, cy + 6, 2, color);
}

static void drawStatusSymbol(int16_t cx, int16_t cy, uint16_t color, bool success) {
    tft.fillCircle(cx, cy, 30, color);
    tft.drawCircle(cx, cy, 30, UI_WHITE);
    if (success) {
        tft.drawLine(cx - 14, cy, cx - 3, cy + 11, UI_WHITE);
        tft.drawLine(cx - 3, cy + 11, cx + 16, cy - 12, UI_WHITE);
        tft.drawLine(cx - 14, cy + 1, cx - 3, cy + 12, UI_WHITE);
        tft.drawLine(cx - 2, cy + 11, cx + 17, cy - 12, UI_WHITE);
    } else if (color == UI_AMBER) {
        tft.drawFastVLine(cx, cy - 13, 17, UI_WHITE);
        tft.fillCircle(cx, cy + 10, 2, UI_WHITE);
    } else {
        tft.drawLine(cx - 12, cy - 12, cx + 12, cy + 12, UI_WHITE);
        tft.drawLine(cx + 12, cy - 12, cx - 12, cy + 12, UI_WHITE);
    }
}

// Dokunmatik kontrolcu nesnesi
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
static bool _touchArmed = true;
static unsigned long _releaseSince = 0;
static unsigned long _lastTouchTime = 0;
#define TOUCH_RELEASE_STABLE_MS 80
#define TOUCH_MIN_INTERVAL_MS 100
#define TOUCH_PRESSURE_MIN 120
#define TOUCH_PRESSURE_MAX 3500
#define TOUCH_SAMPLE_COUNT 3

// Klavye karakter haritasi (4 satir x 7 sutun)
static const char keyboardChars[4][7] = {
    {'A', 'B', 'C', 'D', 'E', 'F', 'G'},
    {'H', 'I', 'J', 'K', 'L', 'M', 'N'},
    {'O', 'P', 'Q', 'R', 'S', 'T', 'U'},
    {'V', 'W', 'X', 'Y', 'Z', '.', '-'}
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
    }
    return -1;
}

static int getEmployeeDisplayCount(Employee* emps, int totalSlots) {
    if (!emps) return 0;
    int count = 0;
    for (int i = 0; i < totalSlots; i++) {
        if (emps[i].active) count++;
    }
    if (count == 0) {
        for (int i = 0; i < totalSlots; i++) {
            if (emps[i].uid[0] != '\0') count++;
        }
    }
    return count;
}

/**
 * @brief Dokunmatik kontrolcuyu baslatir.
 */
void touchInit() {
    ts.begin();
    ts.setRotation(1);  // Ekran yatay modu ile eslestir
    _touchArmed = true;
    _releaseSince = 0;
    _lastTouchTime = 0;
    Serial.println("[TOUCH] Dokunmatik baslatildi");
}

/**
 * @brief Dokunmatik ekrandan veri okur.
 *        Bir basili tutusu tek dokunma sayar, basinci ve konumu ornekleyerek
 *        hafif dokunmalardaki kararsizligi filtreler.
 */
bool touchRead(int16_t& x, int16_t& y) {
    if (!ts.touched()) {
        if (!_touchArmed) {
            unsigned long now = millis();
            if (_releaseSince == 0) {
                _releaseSince = now;
            } else if (now - _releaseSince >= TOUCH_RELEASE_STABLE_MS) {
                _touchArmed = true;
                _releaseSince = 0;
            }
        }
        return false;
    }

    _releaseSince = 0;
    if (!_touchArmed || millis() - _lastTouchTime < TOUCH_MIN_INTERVAL_MS) {
        return false;
    }

    TS_Point samples[TOUCH_SAMPLE_COUNT];
    uint8_t validSamples = 0;
    for (uint8_t i = 0; i < TOUCH_SAMPLE_COUNT; i++) {
        if (!ts.touched()) continue;
        TS_Point sample = ts.getPoint();
        if (sample.z >= TOUCH_PRESSURE_MIN && sample.z <= TOUCH_PRESSURE_MAX) {
            samples[validSamples++] = sample;
        }
    }
    if (validSamples < 2) {
        return false;
    }

    // Uc ornek arasindan ortanca degeri secerek koordinat sicrama etkisini azalt.
    int16_t rawX;
    int16_t rawY;
    if (validSamples == 2) {
        rawX = (samples[0].x + samples[1].x) / 2;
        rawY = (samples[0].y + samples[1].y) / 2;
    } else {
        for (uint8_t i = 0; i < validSamples; i++) {
            for (uint8_t j = i + 1; j < validSamples; j++) {
                if (samples[j].x < samples[i].x) {
                    TS_Point tmp = samples[i]; samples[i] = samples[j]; samples[j] = tmp;
                }
            }
        }
        rawX = samples[validSamples / 2].x;
        for (uint8_t i = 0; i < validSamples; i++) {
            for (uint8_t j = i + 1; j < validSamples; j++) {
                if (samples[j].y < samples[i].y) {
                    TS_Point tmp = samples[i]; samples[i] = samples[j]; samples[j] = tmp;
                }
            }
        }
        rawY = samples[validSamples / 2].y;
    }

    _touchArmed = false;
    _lastTouchTime = millis();

    // Ham dokunma koordinatlarini ekran piksellerine donustur
    // Panel baglantisinda her iki eksen ters oldugu icin yonu koru.
    long mx = map(rawX, TS_MINX, TS_MAXX, SCREEN_W - 1, 0);
    long my = map(rawY, TS_MINY, TS_MAXY, SCREEN_H - 1, 0);

    // Ekran sinirlari icinde tut
    if (mx < 0) mx = 0;
    if (mx >= SCREEN_W) mx = SCREEN_W - 1;
    if (my < 0) my = 0;
    if (my >= SCREEN_H) my = SCREEN_H - 1;

    x = (int16_t)mx;
    y = (int16_t)my;

    Serial.printf("[TOUCH] HAM: x=%d y=%d -> EKRAN: x=%d y=%d\n", rawX, rawY, x, y);

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
    const char* tStr = (timeStr && timeStr[0]) ? timeStr : "--:--:--";
    bool timeChanged = needFullDraw || strcmp(tStr, _lastTimeStr) != 0;

    if (needFullDraw) {
        displayClear(UI_BG);
        tft.fillRect(0, 0, SCREEN_W, 32, UI_PANEL);
        tft.fillRect(0, 0, 5, 32, UI_TEAL);
        drawTextFont(14, 23, "CIGMAMAM", UI_WHITE);
        drawWifiMark(286, 13, wifiOn);
        drawSmallText(264, 27, wifiOn ? "ONLINE" : "OFFLINE", wifiOn ? UI_TEAL : UI_RED);

        // Saat paneli: RFID kart simgesi, buyuk dijital saat ve kisa kullanim ipucu.
        tft.fillRoundRect(12, 40, 296, 72, 12, UI_PANEL);
        tft.drawRoundRect(12, 40, 296, 72, 12, UI_PANEL_2);
        tft.fillRoundRect(22, 51, 48, 48, 10, UI_PANEL_2);
        drawNfcIcon(43, 75, UI_TEAL, 1);
        drawSmallText(84, 56, "SISTEM SAATI", UI_MUTED);
        drawSmallText(84, 104, "KART OKUTARAK ISLEM YAPIN", UI_MUTED);

        tft.fillRoundRect(12, 120, 144, 38, 9, UI_PANEL);
        tft.drawRoundRect(12, 120, 144, 38, 9, UI_PANEL_2);
        tft.fillRoundRect(164, 120, 144, 38, 9, UI_PANEL);
        tft.drawRoundRect(164, 120, 144, 38, 9, UI_PANEL_2);
        drawSmallText(87, 137, "KAYITLI", UI_MUTED);
        drawSmallText(87, 150, "PERSONEL", UI_WHITE);
        drawPersonIcon(29, 139, UI_TEAL);
        drawSmallText(237, 137, "ICERIDE", UI_MUTED);
        drawSmallText(237, 150, "PERSONEL", UI_WHITE);
        drawPersonIcon(178, 139, UI_AMBER);

        tft.drawFastHLine(30, 176, 32, UI_TEAL);
        drawSmallText(70, 180, "KARTINIZI OKUTUN", UI_WHITE);
        tft.drawFastHLine(258, 176, 32, UI_TEAL);

        drawUiButton(12, 190, 144, 40, "SENKRONIZE", UI_PANEL_2, UI_TEAL, &FreeSansBold9pt7b);
        drawUiButton(164, 190, 144, 40, "+ PERSONEL", UI_PANEL_2, UI_AMBER, &FreeSansBold9pt7b);

        _mainScreenDrawn = true;
    }

    if (timeChanged) {
        tft.fillRect(82, 61, 218, 35, UI_PANEL);
        drawTextFont(84, 91, tStr, UI_WHITE, &FreeSansBold18pt7b);
        strncpy(_lastTimeStr, tStr, sizeof(_lastTimeStr) - 1);
    }

    if (needFullDraw || totalEmp != _lastTotalEmp || activeEmp != _lastActiveEmp) {
        char totalBuf[8];
        char activeBuf[8];
        snprintf(totalBuf, sizeof(totalBuf), "%02d", totalEmp);
        snprintf(activeBuf, sizeof(activeBuf), "%02d", activeEmp);
        tft.fillRect(46, 125, 40, 28, UI_PANEL);
        tft.fillRect(196, 125, 40, 28, UI_PANEL);
        drawTextFont(48, 147, totalBuf, UI_TEAL);
        drawTextFont(198, 147, activeBuf, UI_AMBER);
        _lastTotalEmp = totalEmp;
        _lastActiveEmp = activeEmp;
    }

    if (wifiOn != _lastWifiOn && !needFullDraw) {
        tft.fillRect(263, 6, 55, 25, UI_PANEL);
        drawWifiMark(286, 13, wifiOn);
        drawSmallText(264, 27, wifiOn ? "ONLINE" : "OFFLINE", wifiOn ? UI_TEAL : UI_RED);
        _lastWifiOn = wifiOn;
    } else if (needFullDraw) {
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
    displayClear(UI_BG);
    drawScreenHeader("PERSONEL ISLEMI", UI_AMBER);
    tft.fillRoundRect(14, 38, 292, 42, 8, UI_PANEL);
    tft.drawRoundRect(14, 38, 292, 42, 8, UI_PANEL_2);
    drawSmallText(24, 46, "PERSONEL:", UI_MUTED);
    drawTextFont(24, 70, name ? name : "Bilinmiyor", UI_WHITE, &FreeSansBold9pt7b);
    drawCenteredFont(96, "NASIL DEVAM EDECEKSINIZ?", UI_MUTED, &FreeSansBold9pt7b);

    drawUiButton(12, 108, 144, 114, "MOLA", UI_PANEL, UI_AMBER, &FreeSansBold18pt7b);
    drawUiButton(164, 108, 144, 114, "CIKIS", UI_PANEL, UI_RED, &FreeSansBold18pt7b);
}

/**
 * @brief Secim ekrani dokunma kontrolu (0=yok, 1=mola, 2=cikis).
 */
int uiCheckSelectionTouch(int16_t x, int16_t y) {
    if (isInRect(x, y, 12, 108, 144, 114, 4)) {
        return 1;  // MOLA
    }
    if (isInRect(x, y, 164, 108, 144, 114, 4)) {
        return 2;  // CIKIS
    }
    return 0;
}

/**
 * @brief Bilgi/Onay bildirim ekranini cizer.
 */
void uiDrawInfoScreen(const char* name, const char* msg, uint16_t color) {
    bool success = color == COLOR_SUCCESS;
    uint16_t accent = success ? UI_TEAL : ((color == COLOR_ERROR || color == COLOR_CIKIS_BTN) ? UI_RED : UI_AMBER);
    displayClear(UI_BG);
    drawScreenHeader(success ? "ISLEM TAMAM" : "BILGILENDIRME", accent);
    if (name && name[0]) drawCenteredFont(55, name, UI_MUTED);
    drawStatusSymbol(SCREEN_W / 2, 103, accent, success);
    drawCenteredFont(163, msg ? msg : "", UI_WHITE);
    drawSmallText(70, 205, "DEVAM ETMEK ICIN DOKUNUN", UI_MUTED);
}

/**
 * @brief Hata ekranini cizer.
 */
void uiDrawErrorScreen(const char* msg) {
    displayClear(UI_BG);
    drawScreenHeader("BIR SORUN OLDU", UI_RED);
    drawStatusSymbol(SCREEN_W / 2, 98, UI_RED, false);
    drawCenteredFont(160, msg ? msg : "Islem basarisiz", UI_WHITE);
    drawSmallText(70, 205, "KARTI YENIDEN OKUTUN", UI_MUTED);
}

/**
 * @brief Yonetici ana menusunu cizer.
 */
void uiDrawAdminMenu() {
    displayClear(UI_BG);
    drawScreenHeader("YONETICI PANELI", UI_TEAL);
    const char* menuLabels[5] = {"PERSONEL EKLE", "PERSONEL SIL", "MESAI SAATLERI", "PERSONEL LISTESI", "ANA EKRANA DON"};
    const char* menuMarks[5] = {"+", "-", "H", "L", "X"};
    for (int i = 0; i < 5; i++) {
        int16_t by = 38 + i * 38;
        uint16_t accent = (i == 4) ? UI_MUTED : (i == 2 ? UI_AMBER : UI_TEAL);
        tft.fillRoundRect(14, by + 3, 292, 34, 8, 0x0000);
        tft.fillRoundRect(14, by, 292, 34, 8, UI_PANEL);
        tft.drawRoundRect(14, by, 292, 34, 8, UI_PANEL_2);
        tft.fillCircle(35, by + 17, 11, accent);
        drawCenteredInRect(24, by + 5, 22, 23, menuMarks[i], UI_BG);
        drawTextFont(57, by + 23, menuLabels[i], UI_WHITE);
    }
}

/**
 * @brief Yonetici menusu dokunma kontrolu (0-4 menu indeksi, -1=yok).
 */
int uiCheckAdminTouch(int16_t x, int16_t y) {
    for (int i = 0; i < 5; i++) {
        int16_t by = 38 + i * 38;
        if (isInRect(x, y, 14, by, 292, 34, 2)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Personel listesi ekranini cizer (sayfa basina 4 kayit).
 */
void uiDrawEmployeeList(Employee* emps, int totalSlots, int page) {
    displayClear(UI_BG);
    drawScreenHeader("PERSONEL LISTESI", UI_TEAL);
    int employeeTotal = getEmployeeDisplayCount(emps, totalSlots);

    if (employeeTotal == 0) {
        drawCenteredFont(119, "KAYIT BULUNAMADI", UI_MUTED);
    }

    for (int r = 0; r < 4; r++) {
        int idx = getEmployeeIndexOnPage(emps, totalSlots, page, r);
        if (idx >= 0) {
            int16_t rowY = 38 + r * 37;
            tft.fillRoundRect(12, rowY, 296, 34, 7, UI_PANEL);
            tft.drawRoundRect(12, rowY, 296, 34, 7, UI_PANEL_2);

            // Durum rengi ve metni
            uint16_t stColor = UI_MUTED;
            const char* stText = "DISARIDA";
            if (emps[idx].state == STATE_CALISIYOR) {
                stColor = UI_TEAL;
                stText = "CALISIYOR";
            } else if (emps[idx].state == STATE_MOLADA) {
                stColor = UI_AMBER;
                stText = "MOLADA";
            }

            tft.fillRoundRect(17, rowY + 7, 4, 20, 2, stColor);
            drawSmallText(29, rowY + 8, emps[idx].name, UI_WHITE);
            drawSmallText(29, rowY + 22, emps[idx].uid, UI_MUTED);
            drawSmallText(SCREEN_W - 80, rowY + 19, stText, stColor);
        }
    }

    // Gezinme butonlari
    int pageCount = (employeeTotal + 3) / 4;
    uint16_t previousColor = page > 0 ? UI_TEAL : UI_PANEL_2;
    uint16_t nextColor = page + 1 < pageCount ? UI_TEAL : UI_PANEL_2;
    drawUiButton(12, 192, 92, 36, "< ONCEKI", previousColor, UI_TEAL, &FreeSansBold9pt7b);
    drawUiButton(114, 192, 92, 36, "GERI", UI_PANEL, UI_AMBER, &FreeSansBold9pt7b);
    drawUiButton(216, 192, 92, 36, "SONRAKI >", nextColor, UI_TEAL, &FreeSansBold9pt7b);
}

/**
 * @brief Personel listesi dokunma kontrolu (emp index, -2=prev, -3=next, -4=back, -1=yok).
 */
int uiCheckListTouch(int16_t x, int16_t y, int page, int totalSlots, Employee* emps) {
    // Onceki sayfa butonu
    if (isInRect(x, y, 12, 192, 92, 36, 3)) {
        return -2;
    }
    // Geri butonu
    if (isInRect(x, y, 114, 192, 92, 36, 3)) {
        return -4;
    }
    // Sonraki sayfa butonu
    if (isInRect(x, y, 216, 192, 92, 36, 3)) {
        return -3;
    }

    // Personel satirlari (0..3)
    for (int r = 0; r < 4; r++) {
        int16_t rowY = 38 + r * 37;
        if (isInRect(x, y, 12, rowY, 296, 34, 1)) {
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
    displayClear(UI_BG);
    drawScreenHeader("PERSONEL KAYDI", UI_AMBER);
    tft.fillRoundRect(14, 42, 292, 61, 10, UI_PANEL);
    tft.drawRoundRect(14, 42, 292, 61, 10, UI_PANEL_2);
    drawNfcIcon(43, 72, UI_TEAL, 1);
    drawSmallText(78, 60, "KART UID", UI_MUTED);
    drawTextFont(78, 86, (uid && uid[0]) ? uid : "---", UI_WHITE);
    drawCenteredFont(132, "PERSONEL ADINI GIRIN", UI_WHITE);
    drawCenteredFont(154, "VE DEVAM EDIN", UI_MUTED);
    drawUiButton(14, 180, 142, 42, "EKLE", UI_PANEL, UI_TEAL, &FreeSansBold9pt7b);
    drawUiButton(164, 180, 142, 42, "GERI", UI_PANEL, UI_RED, &FreeSansBold9pt7b);
}

/**
 * @brief Personel ekleme ekrani dokunma kontrolu (1=ekle, 2=geri, 0=yok).
 */
int uiCheckAddEmployeeTouch(int16_t x, int16_t y) {
    if (isInRect(x, y, 14, 180, 142, 42, 4)) {
        return 1;  // EKLE
    }
    if (isInRect(x, y, 164, 180, 142, 42, 4)) {
        return 2;  // GERI
    }
    return 0;
}

/**
 * @brief Sanal klavye ekranini cizer.
 */
void uiDrawKeyboard(const char* currentText) {
    displayClear(UI_BG);
    drawScreenHeader("PERSONEL ADI", UI_TEAL);
    tft.fillRoundRect(12, 39, 296, 30, 7, UI_PANEL);
    tft.drawRoundRect(12, 39, 296, 30, 7, UI_PANEL_2);
    char dispBuf[36];
    snprintf(dispBuf, sizeof(dispBuf), "%s_", currentText ? currentText : "");
    if (strlen(dispBuf) > 23) drawSmallText(21, 51, dispBuf, UI_WHITE);
    else drawTextFont(21, 61, dispBuf, UI_WHITE);

    for (int r = 0; r < 4; r++) {
        int16_t ky = 73 + r * 30;
        for (int c = 0; c < 7; c++) {
            int16_t kx = 7 + c * 44;
            char kLabel[2] = { keyboardChars[r][c], '\0' };
            drawUiButton(kx, ky, 40, 27, kLabel, UI_PANEL, UI_PANEL_2);
        }
    }

    drawUiButton(10, 199, 90, 34, "BOSLUK", UI_PANEL_2, UI_MUTED);
    drawUiButton(115, 199, 90, 34, "SIL", UI_PANEL, UI_AMBER);
    drawUiButton(220, 199, 90, 34, "TAMAM", UI_PANEL, UI_TEAL);
}

/**
 * @brief Klavye ekrani dokunma kontrolu (karakter, '\b'=sil, '\n'=tamam, 0=yok).
 */
char uiCheckKeyboardTouch(int16_t x, int16_t y) {
    // Alt islem butonlari kontrolu
    if (isInRect(x, y, 10, 199, 90, 34, 4)) {
        return ' ';   // Bosluk
    }
    if (isInRect(x, y, 115, 199, 90, 34, 4)) {
        return '\b';  // Backspace / Sil
    }
    if (isInRect(x, y, 220, 199, 90, 34, 4)) {
        return '\n';  // Tamam / Onay
    }

    // Karakter tuslari matrisi kontrolu
    for (int r = 0; r < 4; r++) {
        int16_t ky = 73 + r * 30;
        if (y >= ky - 1 && y < ky + 27 + 1) {
            for (int c = 0; c < 7; c++) {
                int16_t kx = 7 + c * 44;
                if (x >= kx - 2 && x < kx + 40 + 2) {
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
    displayClear(UI_BG);
    drawScreenHeader("MESAI AYARLARI", UI_AMBER);
    tft.fillRoundRect(12, 38, 296, 62, 9, UI_PANEL);
    tft.drawRoundRect(12, 38, 296, 62, 9, UI_PANEL_2);
    tft.fillRoundRect(12, 106, 296, 62, 9, UI_PANEL);
    tft.drawRoundRect(12, 106, 296, 62, 9, UI_PANEL_2);

    char timeBuf[32];
    drawSmallText(24, 52, "BASLANGIC", UI_MUTED);
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", cfg.startHour, cfg.startMinute);
    drawTextFont(24, 86, timeBuf, UI_WHITE, &FreeSansBold18pt7b);
    drawSmallText(24, 120, "BITIS", UI_MUTED);
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", cfg.endHour, cfg.endMinute);
    drawTextFont(24, 154, timeBuf, UI_WHITE, &FreeSansBold18pt7b);

    // Butonlar ve metinler
    const char* adjustLabels[4] = {"+H", "-H", "+M", "-M"};
    const int16_t adjustX[4] = {156, 194, 232, 270};
    for (int i = 0; i < 4; i++) {
        drawUiButton(adjustX[i], 55, 34, 34, adjustLabels[i], UI_PANEL_2, i < 2 ? UI_TEAL : UI_AMBER, &FreeSansBold9pt7b);
        drawUiButton(adjustX[i], 123, 34, 34, adjustLabels[i], UI_PANEL_2, i < 2 ? UI_TEAL : UI_AMBER, &FreeSansBold9pt7b);
    }
    drawUiButton(14, 183, 142, 42, "KAYDET", UI_PANEL, UI_TEAL, &FreeSansBold9pt7b);
    drawUiButton(164, 183, 142, 42, "GERI", UI_PANEL, UI_RED, &FreeSansBold9pt7b);
}

/**
 * @brief Mesai ayari dokunma kontrolu (0-7=saat/dk butonlari, 8=kaydet, 9=geri, -1=yok).
 */
int uiCheckShiftTouch(int16_t x, int16_t y) {
    const int16_t adjustX[4] = {156, 194, 232, 270};
    for (int i = 0; i < 4; i++) {
        if (isInRect(x, y, adjustX[i], 55, 34, 34, 2)) return i;
        if (isInRect(x, y, adjustX[i], 123, 34, 34, 2)) return i + 4;
    }
    if (isInRect(x, y, 14, 183, 142, 42, 4)) return 8;
    if (isInRect(x, y, 164, 183, 142, 42, 4)) return 9;

    return -1;
}

/**
 * @brief Personel silme onay ekranini cizer.
 */
void uiDrawDeleteConfirm(const char* name) {
    displayClear(UI_BG);
    drawScreenHeader("PERSONELI SIL", UI_RED);
    tft.drawTriangle(160, 49, 136, 91, 184, 91, UI_RED);
    drawCenteredFont(84, "!", UI_WHITE, &FreeSansBold18pt7b);
    drawCenteredFont(119, (name && name[0]) ? name : "PERSONEL", UI_WHITE);
    drawCenteredFont(146, "SILINSIN MI?", UI_MUTED);
    drawUiButton(14, 174, 142, 46, "EVET, SIL", UI_PANEL, UI_TEAL, &FreeSansBold9pt7b);
    drawUiButton(164, 174, 142, 46, "VAZGEC", UI_PANEL, UI_RED, &FreeSansBold9pt7b);
}

/**
 * @brief Personel silme onay ekrani dokunma kontrolu (1=evet, 2=hayir, 0=yok).
 */
int uiCheckDeleteTouch(int16_t x, int16_t y) {
    if (isInRect(x, y, 14, 174, 142, 46, 4)) {
        return 1;  // EVET
    }
    if (isInRect(x, y, 164, 174, 142, 46, 4)) {
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
    displayClear(UI_BG);
    drawScreenHeader("YENI KART KAYDI", UI_TEAL);
    tft.drawCircle(160, 95, 46, UI_PANEL_2);
    tft.drawCircle(160, 95, 38, UI_TEAL);
    tft.fillRoundRect(132, 78, 56, 34, 6, UI_PANEL);
    tft.drawRoundRect(132, 78, 56, 34, 6, UI_WHITE);
    tft.drawFastHLine(140, 88, 18, UI_TEAL);
    tft.drawFastHLine(140, 95, 12, UI_TEAL);
    tft.drawCircle(172, 95, 3, UI_AMBER);
    tft.drawCircle(178, 95, 6, UI_AMBER);

    drawCenteredFont(150, "KARTI OKUTUN", UI_WHITE, &FreeSansBold12pt7b);
    drawCenteredFont(172, "YENI PERSONELIN KARTINI YAKLASTIRIN", UI_MUTED, &FreeSansBold9pt7b);
    drawUiButton(80, 192, 160, 36, "IPTAL", UI_PANEL, UI_RED, &FreeSansBold9pt7b);
}

/**
 * @brief Ana ekrandaki "+ PERSONEL" butonuna dokunulup dokunulmadigini kontrol eder.
 */
bool uiCheckRegisterButtonTouch(int16_t x, int16_t y) {
    return isInRect(x, y, 164, 190, 144, 40, 4);
}

/**
 * @brief Ana ekrandaki "SENKRONIZE" butonuna dokunulup dokunulmadigini kontrol eder.
 */
bool uiCheckSyncButtonTouch(int16_t x, int16_t y) {
    return isInRect(x, y, 12, 190, 144, 40, 4);
}
