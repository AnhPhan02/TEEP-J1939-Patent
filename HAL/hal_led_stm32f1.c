/*
 * =============================================================================
 * FILE : hal_led_stm32f1.c
 * WHAT : hal_led.h port - status LED on a GPIO (RM0008 9.2).
 * =============================================================================
 */

#include "hal_led.h"
#include "hal_board_cfg.h"

void hal_led_init(void)
{
    RCC->APB2ENR |= BOARD_LED_PORT_CLK;
    hal_led_set(false);
    board_gpio_config(BOARD_LED_PORT, BOARD_LED_PIN, GPIO_CFG_OUT_PP_2MHZ);
}

void hal_led_set(bool on)
{
    bool drive_high = BOARD_LED_ACTIVE_LOW ? !on : on;
    /* BSRR: low half sets, high half resets - atomic, no read-modify-write */
    BOARD_LED_PORT->BSRR = drive_high ? (1u << BOARD_LED_PIN) : (1u << (BOARD_LED_PIN + 16u));
}
