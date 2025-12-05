#pragma once
#include <Arduino.h>
#include <BLESecurity.h>
#include "esp_gap_ble_api.h"

class MySecurity : public BLESecurityCallbacks
{
public:
    uint32_t onPassKeyRequest() override;
    void onPassKeyNotify(uint32_t pass_key) override;
    bool onConfirmPIN(uint32_t) override;
    bool onSecurityRequest() override;
    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override;
};
