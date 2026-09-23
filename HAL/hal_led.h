/*
 * =============================================================================
 * FILE : hal_led.h
 * WHAT : On-board status LED.
 * =============================================================================
 */

#ifndef HAL_LED_H
#define HAL_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void hal_led_init(void);
void hal_led_set(bool on);

#ifdef __cplusplus
}
#endif

#endif /* HAL_LED_H */
