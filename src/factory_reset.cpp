#include "factory_reset.h"

#include <settings.h>
#include "pins.h"

const uint32_t FACTORY_RESET_HOLD_MS = 10000;

/**
 * @brief true if the button is held for FACTORY_RESET_HOLD_MS
 */
static bool isFactoryResetRequested() {
    if (digitalRead(FACTORY_RESET_PIN) != LOW) return false;

    uint32_t start = millis();
    while (digitalRead(FACTORY_RESET_PIN) == LOW) {
        if (millis() - start >= FACTORY_RESET_HOLD_MS) return true;
        delay(100);
    }
    return false;
}

/**
 * @brief Blink the LED rapidly to confirm the reset
 */
static void indicateFactoryReset() {
    for (int i = 0; i < 5; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
    }
}

void handleFactoryReset() {
    if (!isFactoryResetRequested()) return;

    Serial.println("Factory reset");
    indicateFactoryReset();
    settingsDefaults(currentSettings);
    saveSettings();
    Serial.flush();
    rp2040.reboot();
}
