#include "buzzer.h"
#include "config.h"

// Maksimum desen adim sayisi
static const uint8_t MAX_BEEP_STEPS = 10;

// Desen durumlari: cift indeksler SES ACIK, tek indeksler SES KAPALI
static uint16_t beepPattern[MAX_BEEP_STEPS];
static uint8_t beepCount = 0;
static uint8_t beepIndex = 0;
static unsigned long beepStartTime = 0;
static bool isBeeping = false;

// Dahili desen baslatma fonksiyonu
static void startPattern(const uint16_t* pattern, uint8_t count) {
  if (count == 0 || count > MAX_BEEP_STEPS) return;

  for (uint8_t i = 0; i < count; i++) {
    beepPattern[i] = pattern[i];
  }
  beepCount = count;
  beepIndex = 0;
  beepStartTime = millis();
  isBeeping = true;

  // Ilk adim (indeks 0) her zaman ses aciktir
  ledcWriteTone(BUZZER_CHANNEL, BUZZER_FREQ);
}

void buzzerInit() {
  // ESP32 LEDC PWM yapilandirmasi (8-bit cozunurluk)
  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWriteTone(BUZZER_CHANNEL, 0);

  isBeeping = false;
  beepCount = 0;
  beepIndex = 0;
  beepStartTime = 0;
}

void buzzerSuccess() {
  // Basarili islem: Yumusak, cok kisa kibar bip (35ms)
  static const uint16_t successPattern[] = {35};
  startPattern(successPattern, 1);
}

void buzzerError() {
  // Hata bildirimi: 50ms ON, 50ms OFF, 50ms ON
  static const uint16_t errorPattern[] = {50, 50, 50};
  startPattern(errorPattern, 3);
}

void buzzerWarning() {
  // Uyari bildirimi: 40ms ON, 40ms OFF, 40ms ON
  static const uint16_t warningPattern[] = {40, 40, 40};
  startPattern(warningPattern, 3);
}

void buzzerTick() {
  if (!isBeeping) return;

  unsigned long currentMillis = millis();
  if (currentMillis - beepStartTime >= beepPattern[beepIndex]) {
    beepIndex++;
    if (beepIndex >= beepCount) {
      // Desen tamamlandi, buzzeri kapat
      ledcWriteTone(BUZZER_CHANNEL, 0);
      isBeeping = false;
    } else {
      beepStartTime = currentMillis;
      // Cift indeksler ON, tek indeksler OFF
      if ((beepIndex % 2) == 0) {
        ledcWriteTone(BUZZER_CHANNEL, BUZZER_FREQ);
      } else {
        ledcWriteTone(BUZZER_CHANNEL, 0);
      }
    }
  }
}
