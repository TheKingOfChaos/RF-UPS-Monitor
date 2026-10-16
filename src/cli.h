#pragma once

#include <Arduino.h>

/**
 * @brief Print the serial menu
 */
void cliBegin();

/**
 * @brief Poll the serial port and handle menu input (non-blocking)
 * Call from loop()
 */
void cliProcess();

/**
 * @brief Print the current interface addresses (implemented in main.cpp)
 */
void printNetworkInfo();
