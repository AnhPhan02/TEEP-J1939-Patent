/*
 * =============================================================================
 * FILE : j1939_arduino_generator.ino
 * WHAT : Arduino framework entry. All logic lives in APP/MID/HAL.
 * =============================================================================
 */

#include "app_main.h"

void setup()
{
    App_Init();
}

void loop()
{
    App_Run();
}
