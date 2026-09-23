/*
 * =============================================================================
 * FILE : hal_time.h
 * WHAT : System tick (ms) and blocking delay.
 * =============================================================================
 */

#ifndef HAL_TIME_H
#define HAL_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint32_t hal_time_ms(void);
void     hal_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIME_H */
