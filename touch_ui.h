#pragma once

#include "config.h"

// Dokunmatik panel baslatma ve okuma fonksiyonlari
void touchInit();
bool touchRead(int16_t& x, int16_t& y);  // Dokunma varsa true, ekran koordinatlari

// Ekran cizim fonksiyonlari
void uiDrawMainScreen(const char* timeStr, int totalEmp, int activeEmp, bool wifiOn);
void uiResetMainScreen();  // Baska ekrandan donuste tam yeniden cizim icin
void uiDrawSelectionScreen(const char* name);           // MOLA / CIKIS secimi
void uiDrawInfoScreen(const char* name, const char* msg, uint16_t color);
void uiDrawErrorScreen(const char* msg);
void uiDrawAdminMenu();
void uiDrawEmployeeList(Employee* emps, int totalSlots, int page);
void uiDrawAddEmployeeScreen(const char* uid);
void uiDrawKeyboard(const char* currentText);
void uiDrawShiftSettings(ShiftConfig& cfg);
void uiDrawDeleteConfirm(const char* name);
void uiDrawRegisterCardScreen();  // Hizli personel kayit ekrani

// Dokunma algilama fonksiyonlari
int uiCheckSelectionTouch(int16_t x, int16_t y);  // 0=yok, 1=mola, 2=cikis
int uiCheckAdminTouch(int16_t x, int16_t y);      // Menu item index (0-4), -1=yok
int uiCheckListTouch(int16_t x, int16_t y, int page, int totalSlots, Employee* emps);  // employee index, -1=yok, -2=prev page, -3=next page, -4=back
char uiCheckKeyboardTouch(int16_t x, int16_t y);  // karakter, '\b'=sil, '\n'=tamam, 0=yok
int uiCheckShiftTouch(int16_t x, int16_t y);       // 0-3=start +/-, 4-7=end +/-, 8=kaydet, 9=geri, -1=yok
int uiCheckDeleteTouch(int16_t x, int16_t y);      // 1=evet, 2=hayir, 0=yok
int uiCheckAddEmployeeTouch(int16_t x, int16_t y); // 1=ekle, 2=geri, 0=yok
bool uiCheckRegisterButtonTouch(int16_t x, int16_t y);  // Ana ekrandaki + butonu
