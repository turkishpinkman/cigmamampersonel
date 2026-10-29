#pragma once

#include <Arduino.h>
#include "config.h"

// LittleFS'den personel listesini ve durumlarini yukler
void employeeInit();

// UID ile personeli bulur, dizi indexini dondurur (-1 = bulunamadi)
int employeeFind(const char* uid);

// Belirtilen indexteki personele pointer dondurur (gecersiz/inaktif ise nullptr)
Employee* employeeGet(int index);

// Yeni personel ekler
bool employeeAdd(const char* uid, const char* name);

// Personeli inaktif olarak isaretler (silme islemi)
bool employeeRemove(int index);

// Personel durumunu ve zaman damgasini gunceller
void employeeSetState(int index, EmployeeState state, unsigned long timestamp);

// Toplam aktif personel sayisini dondurur
int employeeCount();

// Su an iceride olan personel sayisini dondurur (CALISIYOR + MOLADA)
int employeeActiveCount();

// Tum personel dizisine erisim saglar
Employee* employeeGetAll();

// Toplam slot kapasitesini dondurur (MAX_EMPLOYEES)
int employeeGetTotalSlots();

// Personel ana listesini LittleFS'e kaydeder
void employeeSave();

// Sadece personel durumlarini LittleFS'e kaydeder (sik yapilan islem)
void employeeSaveStates();

// Gunluk mola sayaclarini sifirlar
void employeeResetDailyCounters();
