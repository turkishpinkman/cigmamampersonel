#pragma once

#include "config.h"

// ============================================================================
// BUZZER MODULU (LEDC PWM TABANLI)
// Non-blocking pasif buzzer kontrolu
// ============================================================================

void buzzerInit();
void buzzerSuccess();   // Tek kisa bip (basarili islem)
void buzzerError();     // Uc kisa bip (hata)
void buzzerWarning();   // Iki kisa bip (uyari)
void buzzerTick();      // Non-blocking guncelleme, loop() icinde cagrilir
