#pragma once
#include "config.h"

// ==========================================
// Firebase Senkronizasyon Modulu
// Firestore REST API ile veri senkronizasyonu
// ==========================================

void syncInit();               // Firebase baglantisini hazirla
void syncTick();               // Periyodik senkronizasyon (loop icinde cagrilir)
bool syncIsConnected();        // Firebase kimlik dogrulama durumu
void syncForceUpload();        // Bekleyen olaylari hemen yukle
void syncUploadPendingEvents();   // Olaylari Firestore'a yukle
void syncDownloadEmployees();     // Personel listesini Firestore'dan indir
void syncDownloadSettings();      // Ayarlari Firestore'dan indir
void syncUploadEmployeeState(const char* uid, EmployeeState state);  // Personel durumunu guncelle
void syncUploadNewEmployee(const char* uid, const char* name);  // Yeni personeli Firestore'a yukle
void syncUploadAllEmployees();     // Cihazdaki kayitli tum personelleri Firestore'a senkronize et
