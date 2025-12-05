#include "security.h"

uint32_t MySecurity::onPassKeyRequest()
{
    Serial.println("Passkey requested. Use 123456.");
    return 123456;
}
void MySecurity::onPassKeyNotify(uint32_t pass_key)
{
    Serial.printf("Passkey displayed: %06u\n", pass_key);
}
bool MySecurity::onConfirmPIN(uint32_t)
{
    Serial.println("PIN auto-confirmed");
    return true;
}
bool MySecurity::onSecurityRequest()
{
    Serial.println("Security request accepted");
    return true;
}
void MySecurity::onAuthenticationComplete(esp_ble_auth_cmpl_t auth)
{
    if (auth.success)
        Serial.println("Bonded and authenticated");
    else
        Serial.println("Authentication failed");
}
