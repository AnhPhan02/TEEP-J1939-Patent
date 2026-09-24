/*
 * =============================================================================
 * FILE : hal_board_cfg.h
 * WHAT : Board-level hardware mapping and clock plan.
 *        Target: STM32F103C8T6 (LQFP48) "Blue Pill" style board,
 *        8 MHz HSE crystal, LED on PC13 (active low).
 *
 * NOTE : Private to the HAL layer.
 * =============================================================================
 */

#ifndef HAL_BOARD_CFG_H
#define HAL_BOARD_CFG_H

#include "stm32f103_reg.h"

/* ---- Clock plan (RM0008 7.2, Figure 8) -------------------------------------
 * Primary : HSE 8 MHz -> PLL x9  -> SYSCLK 72 MHz
 *           AHB /1 = 72 MHz, APB1 /2 = 36 MHz (CAN, max 36), APB2 /1 = 72 MHz (USART1)
 * Fallback: HSI 8 MHz /2 -> PLL x16 -> SYSCLK 64 MHz (HSE missing / not starting)
 *           AHB /1 = 64 MHz, APB1 /2 = 32 MHz, APB2 /1 = 64 MHz
 * Both give exact CAN bit timing at 250 and 500 kbit/s (see hal_can_stm32f1.c).
 * --------------------------------------------------------------------------- */
#define BOARD_HSE_HZ                    8000000u
#define BOARD_HSI_HZ                    8000000u
#define BOARD_PLL_MUL_HSE               9u          /* 8 MHz x 9  = 72 MHz */
#define BOARD_PLL_MUL_HSI               16u         /* 4 MHz x 16 = 64 MHz */
#define BOARD_HSE_STARTUP_LOOPS         200000u     /* ~ several ms at 8 MHz HSI */
#define BOARD_PLL_LOCK_LOOPS            200000u

/* ---- CAN (bxCAN) ----------------------------------------------------------
 * PA11/PA12 are USB D-/D+ on this board -> remap CAN to PB8 (RX) / PB9 (TX).
 * --------------------------------------------------------------------------- */
#define BOARD_CAN_PORT                  GPIOB
#define BOARD_CAN_PORT_CLK              RCC_APB2_IOPB
#define BOARD_CAN_RX_PIN                8u
#define BOARD_CAN_TX_PIN                9u
#define BOARD_CAN_REMAP                 AFIO_MAPR_CAN_REMAP_PB8
#define BOARD_CAN_TX_TIMEOUT_MS         2u          /* Wait for a free Tx mailbox */
#define BOARD_CAN_MODE_LOOPS            100000u     /* INAK/SLAK handshake timeout */

/* ---- Debug port: keep SWD, release JTAG pins (PA15, PB3, PB4) ------------- */
#define BOARD_SWJ_CFG                   AFIO_MAPR_SWJ_CFG_SWD_ONLY

/* ---- Console: USART1 on PA9 (TX) / PA10 (RX) ------------------------------
 * USB CDC cannot be used together with CAN on F103 (shared 512-byte SRAM).
 * --------------------------------------------------------------------------- */
#define BOARD_CONSOLE_USART             USART1
#define BOARD_CONSOLE_USART_CLK         RCC_APB2_USART1
#define BOARD_CONSOLE_PORT              GPIOA
#define BOARD_CONSOLE_PORT_CLK          RCC_APB2_IOPA
#define BOARD_CONSOLE_TX_PIN            9u
#define BOARD_CONSOLE_RX_PIN            10u
#define BOARD_CONSOLE_IRQN              IRQN_USART1

/* ---- Status LED ----------------------------------------------------------- */
#define BOARD_LED_PORT                  GPIOC
#define BOARD_LED_PORT_CLK              RCC_APB2_IOPC
#define BOARD_LED_PIN                   13u
#define BOARD_LED_ACTIVE_LOW            1

/* ---- Interrupt priorities (0 = highest, 15 = lowest) ---------------------- */
#define BOARD_IRQ_PRIO_SYSTICK          2u
#define BOARD_IRQ_PRIO_CONSOLE          6u

/* ---- Helpers -------------------------------------------------------------- */
/* Configure one pin: 4-bit CNF:MODE nibble in CRL (0..7) or CRH (8..15) */
static inline void board_gpio_config(GPIO_TypeDef* port, uint32_t pin, uint32_t cfg)
{
    REG32* cr = (pin < 8u) ? &port->CRL : &port->CRH;
    uint32_t shift = (pin & 7u) * 4u;
    *cr = (*cr & ~(0xFu << shift)) | (cfg << shift);
}

#endif /* HAL_BOARD_CFG_H */
