/*
 * =============================================================================
 * FILE : hal_led_arduino.cpp
 * WHAT : hal_led.h port on top of Arduino digital I/O.
 * =============================================================================
 */

#include "hal_led.h"
#include <Arduino.h>
#include "hal_board_cfg.h"

void hal_led_init(void)
{
    pinMode(BOARD_LED_PIN, OUTPUT);
    digitalWrite(BOARD_LED_PIN, LOW);
}

void hal_led_set(bool on)
{
    digitalWrite(BOARD_LED_PIN, on ? HIGH : LOW);
}
