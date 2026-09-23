/*
 * =============================================================================
 * FILE : hal_time_arduino.cpp
 * WHAT : hal_time.h port on top of Arduino millis()/delay().
 * =============================================================================
 */

#include "hal_time.h"
#include <Arduino.h>

uint32_t hal_time_ms(void)
{
    return millis();
}

void hal_delay_ms(uint32_t ms)
{
    delay(ms);
}
