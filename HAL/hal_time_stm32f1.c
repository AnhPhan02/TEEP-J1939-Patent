/*
 * =============================================================================
 * FILE : hal_time_stm32f1.c
 * WHAT : hal_time.h port - 1 ms SysTick (PM0056 4.5).
 * =============================================================================
 */

#include "hal_time.h"
#include "hal_board_cfg.h"

static volatile uint32_t s_ticks_ms = 0u;

void SysTick_Handler(void)
{
    s_ticks_ms++;
}

void hal_time_init(uint32_t hclk_hz)
{
    SYSTICK->CTRL = 0u;
    SYSTICK->LOAD = (hclk_hz / 1000u) - 1u;
    SYSTICK->VAL = 0u;
    SCB->SHP[11] = NVIC_PRIO(BOARD_IRQ_PRIO_SYSTICK);
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
}

uint32_t hal_time_ms(void)
{
    return s_ticks_ms;
}

void hal_delay_ms(uint32_t ms)
{
    uint32_t start = s_ticks_ms;
    while ((s_ticks_ms - start) < ms) { }
}
