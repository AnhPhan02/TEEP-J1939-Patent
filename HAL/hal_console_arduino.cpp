/*
 * =============================================================================
 * FILE : hal_console_arduino.cpp
 * WHAT : hal_console.h port on top of Arduino Serial.
 * =============================================================================
 */

#include "hal_console.h"
#include <Arduino.h>

void hal_console_init(uint32_t baud)
{
    Serial.begin(baud);
}

int hal_console_read(void)
{
    return (Serial.available() > 0) ? Serial.read() : -1;
}

void hal_console_write(const char* str)
{
    if (str != NULL) {
        Serial.print(str);
    }
}

void hal_console_write_line(const char* str)
{
    if (str != NULL) {
        Serial.println(str);
    } else {
        Serial.println();
    }
}

void hal_console_write_u32(uint32_t value)
{
    Serial.print((unsigned long)value);
}

void hal_console_write_i32(int32_t value)
{
    Serial.print((long)value);
}

void hal_console_write_hex(uint32_t value)
{
    Serial.print((unsigned long)value, HEX);
}

void hal_console_write_float(float value, uint8_t decimals)
{
    Serial.print(value, decimals);
}
