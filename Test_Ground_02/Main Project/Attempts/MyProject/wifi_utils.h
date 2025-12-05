#pragma once
#include <Arduino.h>

extern const char *API_BASE;

String encodeMac(String mac);
void sendGet(String mac, String key, String value);
void sendPost(String mac, String key, String value);
