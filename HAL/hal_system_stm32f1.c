/*
 * =============================================================================
 * FILE : hal_system_stm32f1.c
 * WHAT : hal_system.h port - register-level RCC / FLASH / IWDG (RM0008).
 * =============================================================================
 */

#include "hal_system.h"
#include "hal_board_cfg.h"

static hal_clock_info_t  s_clocks = {
    HAL_CLOCK_SRC_HSI, BOARD_HSI_HZ, BOARD_HSI_HZ, BOARD_HSI_HZ, BOARD_HSI_HZ
};
static hal_reset_cause_t s_reset_cause = HAL_RESET_UNKNOWN;

/* =============================================================================
 * Called from Reset_Handler before .data/.bss init - must not touch globals.
 * Put the clock tree in its reset state (HSI, no PLL) and point VTOR at FLASH.
 * ============================================================================= */
void SystemInit(void)
{
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u) { }
    RCC->CFGR = 0u;                                         /* SYSCLK = HSI, no prescalers */
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_CSSON | RCC_CR_HSEON);
    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CIR = 0x009F0000u;                                 /* Clear all ready flags, no IRQs */
    SCB->VTOR = FLASH_BASE;
}

static bool prv_wait_set(REG32* reg, uint32_t mask, uint32_t loops)
{
    while (loops-- > 0u) {
        if ((*reg & mask) == mask) {
            return true;
        }
    }
    return false;
}

static void prv_latch_reset_cause(void)
{
    uint32_t csr = RCC->CSR;

    /* Order matters: POR also sets PINRSTF */
    if (csr & RCC_CSR_IWDGRSTF)      s_reset_cause = HAL_RESET_IWDG;
    else if (csr & RCC_CSR_WWDGRSTF) s_reset_cause = HAL_RESET_WWDG;
    else if (csr & RCC_CSR_LPWRRSTF) s_reset_cause = HAL_RESET_LOW_POWER;
    else if (csr & RCC_CSR_SFTRSTF)  s_reset_cause = HAL_RESET_SOFTWARE;
    else if (csr & RCC_CSR_PORRSTF)  s_reset_cause = HAL_RESET_POWER_ON;
    else if (csr & RCC_CSR_PINRSTF)  s_reset_cause = HAL_RESET_PIN;
    else                             s_reset_cause = HAL_RESET_UNKNOWN;

    RCC->CSR |= RCC_CSR_RMVF;
}

bool hal_system_init(void)
{
    prv_latch_reset_cause();

    /* 0. Dedicated fault handlers (else all escalate to HardFault), trap divide by zero */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA;
    SCB->CCR |= SCB_CCR_DIV_0_TRP;

    /* 1. FLASH: 2 wait states + prefetch BEFORE raising SYSCLK above 24 MHz */
    FLASH_IF->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2WS;

    /* 2. Try the crystal */
    RCC->CR |= RCC_CR_HSEON;
    bool hse_ok = prv_wait_set(&RCC->CR, RCC_CR_HSERDY, BOARD_HSE_STARTUP_LOOPS);

    uint32_t cfgr = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1 |
                    RCC_CFGR_ADCPRE_DIV6 | RCC_CFGR_USBPRE_DIV1_5;
    uint32_t sysclk;

    if (hse_ok) {
        cfgr |= RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLXTPRE_DIV1 | RCC_CFGR_PLLMUL(BOARD_PLL_MUL_HSE);
        sysclk = BOARD_HSE_HZ * BOARD_PLL_MUL_HSE;
    } else {
        RCC->CR &= ~RCC_CR_HSEON;
        cfgr |= RCC_CFGR_PLLSRC_HSI_DIV2 | RCC_CFGR_PLLMUL(BOARD_PLL_MUL_HSI);
        sysclk = (BOARD_HSI_HZ / 2u) * BOARD_PLL_MUL_HSI;
    }

    /* 3. Prescalers + PLL source (PLL still off), then lock the PLL */
    RCC->CFGR = cfgr;
    RCC->CR |= RCC_CR_PLLON;
    if (!prv_wait_set(&RCC->CR, RCC_CR_PLLRDY, BOARD_PLL_LOCK_LOOPS)) {
        RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON);
        RCC->CFGR = 0u;
        return false;                                       /* stay on HSI 8 MHz */
    }

    /* 4. Switch SYSCLK to PLL */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL) { }

    /* 5. Clock security: HSE failure -> NMI (see NMI_Handler) */
    if (hse_ok) {
        RCC->CR |= RCC_CR_CSSON;
    }

    s_clocks.source = hse_ok ? HAL_CLOCK_SRC_HSE_PLL : HAL_CLOCK_SRC_HSI_PLL;
    s_clocks.sysclk_hz = sysclk;
    s_clocks.hclk_hz = sysclk;
    s_clocks.pclk1_hz = sysclk / 2u;
    s_clocks.pclk2_hz = sysclk;
    return true;
}

void hal_system_get_clocks(hal_clock_info_t* out)
{
    if (out != NULL) {
        *out = s_clocks;
    }
}

hal_reset_cause_t hal_system_get_reset_cause(void)
{
    return s_reset_cause;
}

const char* hal_system_reset_cause_name(hal_reset_cause_t cause)
{
    switch (cause) {
        case HAL_RESET_POWER_ON:  return "POWER-ON";
        case HAL_RESET_PIN:       return "NRST PIN";
        case HAL_RESET_SOFTWARE:  return "SOFTWARE";
        case HAL_RESET_IWDG:      return "INDEPENDENT WATCHDOG";
        case HAL_RESET_WWDG:      return "WINDOW WATCHDOG";
        case HAL_RESET_LOW_POWER: return "LOW-POWER";
        default:                  return "UNKNOWN";
    }
}

void hal_system_reset(void)
{
    cpu_dsb();
    SCB->AIRCR = SCB_AIRCR_VECTKEY | (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) | SCB_AIRCR_SYSRESETREQ;
    cpu_dsb();
    for (;;) { }
}

bool hal_system_debugger_attached(void)
{
    return (DHCSR & DHCSR_C_DEBUGEN) != 0u;
}

void hal_watchdog_start(uint32_t timeout_ms)
{
    /* Freeze IWDG while the core is halted by a debugger */
    DBGMCU_CR |= DBGMCU_CR_DBG_IWDG_STOP;

    uint32_t reload = (timeout_ms * (IWDG_LSI_HZ / 1000u)) / 64u;
    if (reload == 0u) reload = 1u;
    if (reload > IWDG_RLR_MAX) reload = IWDG_RLR_MAX;

    IWDG->KR = IWDG_KEY_START;                              /* also starts LSI */
    IWDG->KR = IWDG_KEY_UNLOCK;
    IWDG->PR = IWDG_PR_DIV64;
    IWDG->RLR = reload;
    while (IWDG->SR != 0u) { }                              /* PVU/RVU update done */
    IWDG->KR = IWDG_KEY_RELOAD;
}

void hal_watchdog_feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

/* =============================================================================
 * NMI: raised by the clock security system when the HSE fails at runtime.
 * Hardware already switched SYSCLK to HSI, so every bus clock (and the CAN
 * bit rate) is now wrong - reboot and let hal_system_init() pick a new source.
 * ============================================================================= */
void NMI_Handler(void)
{
    if (RCC->CIR & RCC_CIR_CSSF) {
        RCC->CIR = RCC_CIR_CSSC;
    }
    hal_system_reset();
}
