/*
 * =============================================================================
 * FILE : hal_console_stm32f1.c
 * WHAT : hal_console.h port - USART1 with interrupt-driven RX/TX ring buffers
 *        (RM0008 27.6). Writes never wait on the UART unless the TX buffer
 *        is full, so telemetry does not stall the CAN scheduler.
 * =============================================================================
 */

#include "hal_console.h"
#include "hal_system.h"
#include "hal_board_cfg.h"

#define TX_BUF_SIZE     1024u               /* power of two */
#define RX_BUF_SIZE     128u                /* power of two */

static uint8_t           s_tx_buf[TX_BUF_SIZE];
static volatile uint16_t s_tx_head = 0u;    /* written by thread */
static volatile uint16_t s_tx_tail = 0u;    /* written by ISR */

static uint8_t           s_rx_buf[RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0u;    /* written by ISR */
static volatile uint16_t s_rx_tail = 0u;    /* written by thread */

void USART1_IRQHandler(void)
{
    uint32_t sr = BOARD_CONSOLE_USART->SR;

    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t c = (uint8_t)BOARD_CONSOLE_USART->DR;       /* SR then DR read clears ORE */
        uint16_t next = (uint16_t)((s_rx_head + 1u) & (RX_BUF_SIZE - 1u));
        if (next != s_rx_tail) {
            s_rx_buf[s_rx_head] = c;
            s_rx_head = next;
        }
    }

    if ((BOARD_CONSOLE_USART->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        if (s_tx_tail != s_tx_head) {
            BOARD_CONSOLE_USART->DR = s_tx_buf[s_tx_tail];
            s_tx_tail = (uint16_t)((s_tx_tail + 1u) & (TX_BUF_SIZE - 1u));
        } else {
            BOARD_CONSOLE_USART->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

static void prv_put(char c)
{
    uint16_t next = (uint16_t)((s_tx_head + 1u) & (TX_BUF_SIZE - 1u));
    while (next == s_tx_tail) {
        /* Buffer full: wait for the ISR to drain one byte */
    }
    s_tx_buf[s_tx_head] = (uint8_t)c;
    s_tx_head = next;
    BOARD_CONSOLE_USART->CR1 |= USART_CR1_TXEIE;
}

void hal_console_init(uint32_t baud)
{
    hal_clock_info_t clk;
    hal_system_get_clocks(&clk);

    RCC->APB2ENR |= BOARD_CONSOLE_PORT_CLK | BOARD_CONSOLE_USART_CLK;

    /* TX: AF push-pull; RX: input with pull-up (idle line high) */
    board_gpio_config(BOARD_CONSOLE_PORT, BOARD_CONSOLE_TX_PIN, GPIO_CFG_AF_PP_50MHZ);
    board_gpio_config(BOARD_CONSOLE_PORT, BOARD_CONSOLE_RX_PIN, GPIO_CFG_INPUT_PULL);
    BOARD_CONSOLE_PORT->BSRR = 1u << BOARD_CONSOLE_RX_PIN;

    /* BRR = USARTDIV in 1/16 units = PCLK2 / baud (rounded) */
    BOARD_CONSOLE_USART->CR1 = 0u;
    BOARD_CONSOLE_USART->BRR = (clk.pclk2_hz + baud / 2u) / baud;
    BOARD_CONSOLE_USART->CR2 = 0u;                          /* 1 stop bit */
    BOARD_CONSOLE_USART->CR3 = 0u;
    BOARD_CONSOLE_USART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    nvic_enable_irq(BOARD_CONSOLE_IRQN, BOARD_IRQ_PRIO_CONSOLE);
}

int hal_console_read(void)
{
    if (s_rx_tail == s_rx_head) {
        return -1;
    }
    uint8_t c = s_rx_buf[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) & (RX_BUF_SIZE - 1u));
    return c;
}

void hal_console_write(const char* str)
{
    if (str == NULL) {
        return;
    }
    while (*str != '\0') {
        prv_put(*str++);
    }
}

void hal_console_write_line(const char* str)
{
    hal_console_write(str);
    prv_put('\r');
    prv_put('\n');
}

void hal_console_write_u32(uint32_t value)
{
    char buf[11];
    int i = 0;
    do {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    while (i > 0) {
        prv_put(buf[--i]);
    }
}

void hal_console_write_i32(int32_t value)
{
    if (value < 0) {
        prv_put('-');
        hal_console_write_u32((uint32_t)(-(value + 1)) + 1u);
    } else {
        hal_console_write_u32((uint32_t)value);
    }
}

void hal_console_write_hex(uint32_t value)
{
    static const char k_hex[] = "0123456789ABCDEF";
    bool started = false;
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nib = (value >> shift) & 0xFu;
        if (nib != 0u || started || shift == 0) {
            prv_put(k_hex[nib]);
            started = true;
        }
    }
}

void hal_console_write_float(float value, uint8_t decimals)
{
    if (value != value) {
        hal_console_write("nan");
        return;
    }
    if (decimals > 6u) {
        decimals = 6u;
    }

    if (value < 0.0f) {
        prv_put('-');
        value = -value;
    }

    uint32_t scale = 1u;
    for (uint8_t i = 0; i < decimals; i++) {
        scale *= 10u;
    }

    if (value > 4.0e9f) {
        hal_console_write("ovf");
        return;
    }

    /* Round once at the last printed digit, then split */
    float scaled = value * (float)scale + 0.5f;
    uint32_t int_part = (uint32_t)(scaled / (float)scale);
    uint32_t frac_part = (uint32_t)(scaled - (float)int_part * (float)scale);
    if (frac_part >= scale) {
        int_part++;
        frac_part -= scale;
    }

    hal_console_write_u32(int_part);
    if (decimals > 0u) {
        prv_put('.');
        for (uint32_t div = scale / 10u; div > 0u; div /= 10u) {
            prv_put((char)('0' + (frac_part / div) % 10u));
        }
    }
}
