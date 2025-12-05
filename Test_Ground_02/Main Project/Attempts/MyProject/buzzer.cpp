#include "buzzer.h"

static const int BUZZ_CH = 0;

void buzzer_init() {
  pinMode(BUZZER_PIN, OUTPUT);
  ledcAttach(BUZZER_PIN, BUZZ_CH);
  ledcWriteTone(BUZZ_CH, 0);
}

void buzzer_beep(uint16_t freq, uint16_t ms) {
  ledcWriteTone(BUZZ_CH, freq);
  delay(ms);
  ledcWriteTone(BUZZ_CH, 0);
}
