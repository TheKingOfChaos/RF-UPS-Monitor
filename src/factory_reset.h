#pragma once

/**
 * @brief Check the factory reset button and reset settings after a 10 s hold
 *
 * Blocks while the button is held (up to 10 s). On reset, the LED blinks,
 * factory defaults are saved and the device reboots.
 */
void handleFactoryReset();
