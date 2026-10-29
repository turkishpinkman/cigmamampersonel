#include "display_manager.h"
#include "config.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ILI9341 TFT ekran nesnesi
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

/**
 * @brief Ekrani baslatir, yatay moda (320x240) alir ve arkaplani temizler.
 */
void displayInit() {
    tft.begin();
    tft.setRotation(1);  // Yatay mod (Landscape 320x240)
    tft.fillScreen(COLOR_BG);
    tft.setTextWrap(false);
}

/**
 * @brief Ekrani belirtilen renkle temizler.
 */
void displayClear(uint16_t color) {
    tft.fillScreen(color);
}

/**
 * @brief Belirtilen koordinatlara yazi yazar.
 */
void displayText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size) {
    if (!text) return;
    tft.setTextColor(color);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(text);
}

/**
 * @brief Yaziyi yatay eksende ortalayarak belirtilen y koordinatina yazar.
 */
void displayCentered(int16_t y, const char* text, uint16_t color, uint8_t size) {
    if (!text) return;
    int16_t textWidth = (int16_t)strlen(text) * 6 * size;
    int16_t x = (SCREEN_W - textWidth) / 2;
    if (x < 0) x = 0;
    displayText(x, y, text, color, size);
}

/**
 * @brief Dolu dikdortgen cizer.
 */
void displayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}

/**
 * @brief Kenarlik dikdortgen cizer.
 */
void displayRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.drawRect(x, y, w, h, color);
}

/**
 * @brief Yuvarlatilmis koseli buton cizer ve metnini buton icinde ortalar.
 */
void displayButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t bg, uint16_t fg, uint8_t textSize) {
    tft.fillRoundRect(x, y, w, h, 8, bg);
    tft.drawRoundRect(x, y, w, h, 8, fg);
    if (label && strlen(label) > 0) {
        int16_t textWidth = (int16_t)strlen(label) * 6 * textSize;
        int16_t textHeight = 8 * textSize;
        int16_t tx = x + (w - textWidth) / 2;
        int16_t ty = y + (h - textHeight) / 2;
        displayText(tx, ty, label, fg, textSize);
    }
}

/**
 * @brief Ekranin ust kisminda baslik cubugu cizer.
 */
void displayHeader(const char* text, uint16_t bgColor) {
    tft.fillRect(0, 0, SCREEN_W, 30, bgColor);
    displayCentered(7, text, COLOR_TEXT, 2);
}

/**
 * @brief Ekranin en alt kisminda saat ve WiFi durum cubugu cizer.
 */
void displayStatusBar(const char* timeStr, bool wifiConnected) {
    // Ekranin alt kisminda 20px yuksekliginde durum cubugu
    tft.fillRect(0, SCREEN_H - 20, SCREEN_W, 20, COLOR_DARK_GRAY);
    
    // Sol tarafta zaman bilgisi
    if (timeStr && strlen(timeStr) > 0) {
        displayText(8, SCREEN_H - 14, timeStr, COLOR_TEXT, 1);
    }
    
    // Sag tarafta WiFi durum metni ve gosterge daire
    displayText(SCREEN_W - 55, SCREEN_H - 14, "WiFi", COLOR_TEXT, 1);
    uint16_t wifiColor = wifiConnected ? COLOR_SUCCESS : COLOR_ERROR;
    tft.fillCircle(SCREEN_W - 15, SCREEN_H - 10, 5, wifiColor);
}
