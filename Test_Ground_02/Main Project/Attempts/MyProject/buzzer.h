#pragma once
#include <Arduino.h>

#ifndef BUZZER_PIN
#define BUZZER_PIN 21
#endif

void buzzer_init();
void buzzer_beep(uint16_t freq, uint16_t ms);
