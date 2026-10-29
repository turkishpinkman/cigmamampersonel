#pragma once

#include "config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// Global TFT ekran nesnesi tanimi
extern Adafruit_ILI9341 tft;

// Ekran baslatma ve temel cizim fonksiyonlari
void displayInit();
void displayClear(uint16_t color = COLOR_BG);
void displayText(int16_t x, int16_t y, const char* text, uint16_t color, uint8_t size);
void displayCentered(int16_t y, const char* text, uint16_t color, uint8_t size);
void displayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void displayRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void displayButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t bg, uint16_t fg, uint8_t textSize = 2);
void displayHeader(const char* text, uint16_t bgColor = COLOR_HEADER_BG);
void displayStatusBar(const char* timeStr, bool wifiConnected);
