/*
 * =============================================================================
 * FILE : hal_board_cfg.h
 * WHAT : Board-level hardware mapping (pins, peripheral instances, timeouts).
 *        Target: NUCLEO-F103RB (STM32F103RB), CAN1 remapped to PB8/PB9.
 *
 * NOTE : Private to the HAL layer. Include only from HAL *.c/*.cpp files,
 *        after the STM32 HAL headers (uses GPIOx / CANx symbols).
 * =============================================================================
 */

#ifndef HAL_BOARD_CFG_H
#define HAL_BOARD_CFG_H

/* ---- CAN (bxCAN) ---------------------------------------------------------- */
#define BOARD_CAN_INSTANCE              CAN1
#define BOARD_CAN_CLK_ENABLE()          __HAL_RCC_CAN1_CLK_ENABLE()
#define BOARD_CAN_CLK_DISABLE()         __HAL_RCC_CAN1_CLK_DISABLE()
#define BOARD_CAN_GPIO_CLK_ENABLE()     __HAL_RCC_GPIOB_CLK_ENABLE()
#define BOARD_CAN_PIN_REMAP()           __HAL_AFIO_REMAP_CAN1_2()   /* CAN1 -> PB8/PB9 */

#define BOARD_CAN_TX_PORT               GPIOB
#define BOARD_CAN_TX_PIN                GPIO_PIN_9                  /* Arduino D14 */
#define BOARD_CAN_RX_PORT               GPIOB
#define BOARD_CAN_RX_PIN                GPIO_PIN_8                  /* Arduino D15 */

#define BOARD_CAN_TX_TIMEOUT_MS         2u      /* Wait for a free Tx mailbox */

/* ---- Status LED ----------------------------------------------------------- */
#define BOARD_LED_PIN                   LED_BUILTIN                 /* PA5 on Nucleo */

#endif /* HAL_BOARD_CFG_H */
