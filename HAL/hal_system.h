/*
 * =============================================================================
 * FILE : hal_system.h
 * WHAT : Clock tree, reset cause, watchdog, system reset.
 * =============================================================================
 */

#ifndef HAL_SYSTEM_H
#define HAL_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HAL_CLOCK_SRC_HSE_PLL = 0,      /* Normal: crystal + PLL */
    HAL_CLOCK_SRC_HSI_PLL = 1,      /* Fallback: HSE did not start */
    HAL_CLOCK_SRC_HSI     = 2       /* Degraded: PLL did not lock */
} hal_clock_src_t;

typedef struct {
    hal_clock_src_t source;
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;              /* APB1: CAN, TIM2..4 (x2) */
    uint32_t pclk2_hz;              /* APB2: USART1, GPIO, AFIO */
} hal_clock_info_t;

typedef enum {
    HAL_RESET_POWER_ON = 0,
    HAL_RESET_PIN,
    HAL_RESET_SOFTWARE,
    HAL_RESET_IWDG,
    HAL_RESET_WWDG,
    HAL_RESET_LOW_POWER,
    HAL_RESET_UNKNOWN
} hal_reset_cause_t;

/* Configure FLASH wait states, HSE/PLL and bus prescalers, enable CSS.
 * Also latches and clears the reset cause. Returns false only if the PLL
 * could not be started (system keeps running on HSI 8 MHz). */
bool hal_system_init(void);

void hal_system_get_clocks(hal_clock_info_t* out);
hal_reset_cause_t hal_system_get_reset_cause(void);
const char* hal_system_reset_cause_name(hal_reset_cause_t cause);

void hal_system_reset(void);
bool hal_system_debugger_attached(void);

/* Independent watchdog (LSI ~40 kHz). Once started it cannot be stopped. */
void hal_watchdog_start(uint32_t timeout_ms);
void hal_watchdog_feed(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYSTEM_H */
