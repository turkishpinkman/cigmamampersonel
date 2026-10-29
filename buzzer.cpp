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
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ESP32 LEDC PWM yapilandirmasi (8-bit cozunurluk)
  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  ledcWriteTone(BUZZER_CHANNEL, 0);

  isBeeping = false;
  beepCount = 0;
  beepIndex = 0;
  beepStartTime = 0;
}

void buzzerStop() {
  ledcWriteTone(BUZZER_CHANNEL, 0);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  isBeeping = false;
  beepCount = 0;
  beepIndex = 0;
}

void buzzerSuccess() {
  // Basarili islem: Cok kisa ve hafif bir tiklama sesi (25ms)
  static const uint16_t successPattern[] = {25};
  startPattern(successPattern, 1);
}

void buzzerError() {
  // Hata bildirimi: Kisa iki uyari sesi (30ms ON, 30ms OFF, 30ms ON)
  static const uint16_t errorPattern[] = {30, 30, 30};
  startPattern(errorPattern, 3);
}

void buzzerWarning() {
  // Uyari bildirimi: Kisa iki bip (25ms ON, 25ms OFF, 25ms ON)
  static const uint16_t warningPattern[] = {25, 25, 25};
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
