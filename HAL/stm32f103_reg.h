/*
 * =============================================================================
 * FILE : stm32f103_reg.h
 * WHAT : Minimal register map for STM32F103x8/xB (medium density).
 *        Addresses and bit positions from RM0008 Rev 21:
 *          - Memory map            : Table 3  (section 3.3)
 *          - FLASH_ACR             : section 3.3.3
 *          - RCC                   : section 7.3
 *          - GPIO / AFIO           : section 9.2 / 9.4
 *          - IWDG                  : section 19.4
 *          - bxCAN                 : section 24.9 (Table 181)
 *          - USART                 : section 27.6
 *          - DBGMCU                : section 31.16.3
 *        Cortex-M3 core peripherals (SysTick, NVIC, SCB) from PM0056.
 *
 * NOTE : Private to the HAL layer.
 * =============================================================================
 */

#ifndef STM32F103_REG_H
#define STM32F103_REG_H

#include <stdint.h>
#include <stddef.h>

#define REG32   volatile uint32_t

/* =============================================================================
 * PERIPHERAL STRUCTS
 * ============================================================================= */
typedef struct {
    REG32 CR;           /* 0x00 */
    REG32 CFGR;         /* 0x04 */
    REG32 CIR;          /* 0x08 */
    REG32 APB2RSTR;     /* 0x0C */
    REG32 APB1RSTR;     /* 0x10 */
    REG32 AHBENR;       /* 0x14 */
    REG32 APB2ENR;      /* 0x18 */
    REG32 APB1ENR;      /* 0x1C */
    REG32 BDCR;         /* 0x20 */
    REG32 CSR;          /* 0x24 */
} RCC_TypeDef;

typedef struct {
    REG32 ACR;          /* 0x00 */
} FLASH_TypeDef;

typedef struct {
    REG32 CRL;          /* 0x00 pins 0..7  */
    REG32 CRH;          /* 0x04 pins 8..15 */
    REG32 IDR;          /* 0x08 */
    REG32 ODR;          /* 0x0C */
    REG32 BSRR;         /* 0x10 */
    REG32 BRR;          /* 0x14 */
    REG32 LCKR;         /* 0x18 */
} GPIO_TypeDef;

typedef struct {
    REG32 EVCR;         /* 0x00 */
    REG32 MAPR;         /* 0x04 */
} AFIO_TypeDef;

typedef struct {
    REG32 SR;           /* 0x00 */
    REG32 DR;           /* 0x04 */
    REG32 BRR;          /* 0x08 */
    REG32 CR1;          /* 0x0C */
    REG32 CR2;          /* 0x10 */
    REG32 CR3;          /* 0x14 */
    REG32 GTPR;         /* 0x18 */
} USART_TypeDef;

typedef struct {
    REG32 KR;           /* 0x00 */
    REG32 PR;           /* 0x04 */
    REG32 RLR;          /* 0x08 */
    REG32 SR;           /* 0x0C */
} IWDG_TypeDef;

typedef struct {
    REG32 TIR;          /* identifier */
    REG32 TDTR;         /* DLC + time stamp */
    REG32 TDLR;         /* data bytes 0..3 */
    REG32 TDHR;         /* data bytes 4..7 */
} CAN_TxMailbox_TypeDef;

typedef struct {
    REG32 RIR;
    REG32 RDTR;
    REG32 RDLR;
    REG32 RDHR;
} CAN_FifoMailbox_TypeDef;

typedef struct {
    REG32 FR1;
    REG32 FR2;
} CAN_FilterBank_TypeDef;

typedef struct {
    REG32 MCR;                                  /* 0x000 */
    REG32 MSR;                                  /* 0x004 */
    REG32 TSR;                                  /* 0x008 */
    REG32 RF0R;                                 /* 0x00C */
    REG32 RF1R;                                 /* 0x010 */
    REG32 IER;                                  /* 0x014 */
    REG32 ESR;                                  /* 0x018 */
    REG32 BTR;                                  /* 0x01C */
    uint32_t RESERVED0[88];                     /* 0x020 - 0x17F */
    CAN_TxMailbox_TypeDef   TX[3];              /* 0x180 - 0x1AF */
    CAN_FifoMailbox_TypeDef RX[2];              /* 0x1B0 - 0x1CF */
    uint32_t RESERVED1[12];                     /* 0x1D0 - 0x1FF */
    REG32 FMR;                                  /* 0x200 */
    REG32 FM1R;                                 /* 0x204 */
    uint32_t RESERVED2;                         /* 0x208 */
    REG32 FS1R;                                 /* 0x20C */
    uint32_t RESERVED3;                         /* 0x210 */
    REG32 FFA1R;                                /* 0x214 */
    uint32_t RESERVED4;                         /* 0x218 */
    REG32 FA1R;                                 /* 0x21C */
    uint32_t RESERVED5[8];                      /* 0x220 - 0x23F */
    CAN_FilterBank_TypeDef FB[14];              /* 0x240 - 14 banks on non-connectivity F10x */
} CAN_TypeDef;

typedef struct {
    REG32 CTRL;         /* 0x00 */
    REG32 LOAD;         /* 0x04 */
    REG32 VAL;          /* 0x08 */
    REG32 CALIB;        /* 0x0C */
} SysTick_TypeDef;

typedef struct {
    REG32 CPUID;        /* 0x00 */
    REG32 ICSR;         /* 0x04 */
    REG32 VTOR;         /* 0x08 */
    REG32 AIRCR;        /* 0x0C */
    REG32 SCR;          /* 0x10 */
    REG32 CCR;          /* 0x14 */
    volatile uint8_t SHP[12];   /* 0x18 - 0x23 system handler priorities (SHP[11] = SysTick) */
    REG32 SHCSR;        /* 0x24 */
    REG32 CFSR;         /* 0x28 */
    REG32 HFSR;         /* 0x2C */
    REG32 DFSR;         /* 0x30 */
    REG32 MMFAR;        /* 0x34 */
    REG32 BFAR;         /* 0x38 */
} SCB_TypeDef;

/* =============================================================================
 * BASE ADDRESSES (RM0008 Table 3, PM0056 section 4)
 * ============================================================================= */
#define TIM2_BASE       0x40000000u
#define IWDG_BASE       0x40003000u
#define CAN1_BASE       0x40006400u
#define AFIO_BASE       0x40010000u
#define GPIOA_BASE      0x40010800u
#define GPIOB_BASE      0x40010C00u
#define GPIOC_BASE      0x40011000u
#define USART1_BASE     0x40013800u
#define RCC_BASE        0x40021000u
#define FLASH_R_BASE    0x40022000u
#define FLASH_BASE      0x08000000u

#define SYSTICK_BASE    0xE000E010u
#define NVIC_ISER_BASE  0xE000E100u
#define NVIC_ICER_BASE  0xE000E180u
#define NVIC_IP_BASE    0xE000E400u
#define SCB_BASE        0xE000ED00u
#define DHCSR_ADDR      0xE000EDF0u
#define DBGMCU_CR_ADDR  0xE0042004u

#define RCC             ((RCC_TypeDef*)RCC_BASE)
#define FLASH_IF        ((FLASH_TypeDef*)FLASH_R_BASE)
#define GPIOA           ((GPIO_TypeDef*)GPIOA_BASE)
#define GPIOB           ((GPIO_TypeDef*)GPIOB_BASE)
#define GPIOC           ((GPIO_TypeDef*)GPIOC_BASE)
#define AFIO            ((AFIO_TypeDef*)AFIO_BASE)
#define USART1          ((USART_TypeDef*)USART1_BASE)
#define IWDG            ((IWDG_TypeDef*)IWDG_BASE)
#define CAN1            ((CAN_TypeDef*)CAN1_BASE)
#define SYSTICK         ((SysTick_TypeDef*)SYSTICK_BASE)
#define SCB             ((SCB_TypeDef*)SCB_BASE)
#define NVIC_ISER       ((REG32*)NVIC_ISER_BASE)
#define NVIC_ICER       ((REG32*)NVIC_ICER_BASE)
#define NVIC_IP         ((volatile uint8_t*)NVIC_IP_BASE)
#define DHCSR           (*(REG32*)DHCSR_ADDR)
#define DBGMCU_CR       (*(REG32*)DBGMCU_CR_ADDR)

/* =============================================================================
 * BIT DEFINITIONS
 * ============================================================================= */

/* ---- FLASH_ACR (RM0008 3.3.3) ---- */
#define FLASH_ACR_LATENCY_Msk       (0x7u << 0)
#define FLASH_ACR_LATENCY_0WS       (0x0u << 0)     /* SYSCLK <= 24 MHz */
#define FLASH_ACR_LATENCY_1WS       (0x1u << 0)     /* 24 < SYSCLK <= 48 MHz */
#define FLASH_ACR_LATENCY_2WS       (0x2u << 0)     /* 48 < SYSCLK <= 72 MHz */
#define FLASH_ACR_PRFTBE            (1u << 4)

/* ---- RCC_CR (7.3.1) ---- */
#define RCC_CR_HSION                (1u << 0)
#define RCC_CR_HSIRDY               (1u << 1)
#define RCC_CR_HSEON                (1u << 16)
#define RCC_CR_HSERDY               (1u << 17)
#define RCC_CR_HSEBYP               (1u << 18)
#define RCC_CR_CSSON                (1u << 19)
#define RCC_CR_PLLON                (1u << 24)
#define RCC_CR_PLLRDY               (1u << 25)

/* ---- RCC_CFGR (7.3.2) ---- */
#define RCC_CFGR_SW_Msk             (0x3u << 0)
#define RCC_CFGR_SW_HSI             (0x0u << 0)
#define RCC_CFGR_SW_PLL             (0x2u << 0)
#define RCC_CFGR_SWS_Msk            (0x3u << 2)
#define RCC_CFGR_SWS_PLL            (0x2u << 2)
#define RCC_CFGR_HPRE_DIV1          (0x0u << 4)
#define RCC_CFGR_PPRE1_DIV1         (0x0u << 8)
#define RCC_CFGR_PPRE1_DIV2         (0x4u << 8)
#define RCC_CFGR_PPRE2_DIV1         (0x0u << 11)
#define RCC_CFGR_ADCPRE_DIV6        (0x2u << 14)
#define RCC_CFGR_PLLSRC_HSI_DIV2    (0x0u << 16)
#define RCC_CFGR_PLLSRC_HSE         (0x1u << 16)
#define RCC_CFGR_PLLXTPRE_DIV1      (0x0u << 17)
#define RCC_CFGR_PLLMUL(x)          ((uint32_t)((x) - 2u) << 18)    /* x = 2..16 */
#define RCC_CFGR_USBPRE_DIV1_5      (0x0u << 22)

/* ---- RCC_CIR (7.3.3) ---- */
#define RCC_CIR_CSSF                (1u << 7)
#define RCC_CIR_CSSC                (1u << 23)

/* ---- RCC_APB1RSTR / RCC_APB1ENR (7.3.5 / 7.3.8) ---- */
#define RCC_APB1_CAN                (1u << 25)
#define RCC_APB1_PWR                (1u << 28)

/* ---- RCC_APB2RSTR / RCC_APB2ENR (7.3.4 / 7.3.7) ---- */
#define RCC_APB2_AFIO               (1u << 0)
#define RCC_APB2_IOPA               (1u << 2)
#define RCC_APB2_IOPB               (1u << 3)
#define RCC_APB2_IOPC               (1u << 4)
#define RCC_APB2_USART1             (1u << 14)

/* ---- RCC_CSR (7.3.10) ---- */
#define RCC_CSR_LSION               (1u << 0)
#define RCC_CSR_LSIRDY              (1u << 1)
#define RCC_CSR_RMVF                (1u << 24)
#define RCC_CSR_PINRSTF             (1u << 26)
#define RCC_CSR_PORRSTF             (1u << 27)
#define RCC_CSR_SFTRSTF             (1u << 28)
#define RCC_CSR_IWDGRSTF            (1u << 29)
#define RCC_CSR_WWDGRSTF            (1u << 30)
#define RCC_CSR_LPWRRSTF            (1u << 31)

/* ---- GPIO CRL/CRH nibble = CNF[1:0]:MODE[1:0] (9.2.1 / 9.2.2, Table 20/21) ---- */
#define GPIO_CFG_INPUT_FLOATING     0x4u    /* CNF=01 MODE=00 */
#define GPIO_CFG_INPUT_PULL         0x8u    /* CNF=10 MODE=00, ODR selects up(1)/down(0) */
#define GPIO_CFG_OUT_PP_2MHZ        0x2u    /* CNF=00 MODE=10 */
#define GPIO_CFG_AF_PP_50MHZ        0xBu    /* CNF=10 MODE=11 */

/* ---- AFIO_MAPR (9.4.2) ---- */
#define AFIO_MAPR_CAN_REMAP_Msk     (0x3u << 13)
#define AFIO_MAPR_CAN_REMAP_PA11    (0x0u << 13)
#define AFIO_MAPR_CAN_REMAP_PB8     (0x2u << 13)    /* RX=PB8, TX=PB9 */
#define AFIO_MAPR_SWJ_CFG_Msk       (0x7u << 24)    /* write-only, reads undefined */
#define AFIO_MAPR_SWJ_CFG_FULL      (0x0u << 24)
#define AFIO_MAPR_SWJ_CFG_SWD_ONLY  (0x2u << 24)

/* ---- USART (27.6) ---- */
#define USART_SR_ORE                (1u << 3)
#define USART_SR_RXNE               (1u << 5)
#define USART_SR_TC                 (1u << 6)
#define USART_SR_TXE                (1u << 7)
#define USART_CR1_RE                (1u << 2)
#define USART_CR1_TE                (1u << 3)
#define USART_CR1_RXNEIE            (1u << 5)
#define USART_CR1_TXEIE             (1u << 7)
#define USART_CR1_UE                (1u << 13)

/* ---- IWDG (19.4) ---- */
#define IWDG_KEY_RELOAD             0xAAAAu
#define IWDG_KEY_UNLOCK             0x5555u
#define IWDG_KEY_START              0xCCCCu
#define IWDG_PR_DIV64               0x4u
#define IWDG_RLR_MAX                0xFFFu
#define IWDG_LSI_HZ                 40000u  /* typical; datasheet range 30..60 kHz */

/* ---- bxCAN (24.9) ---- */
#define CAN_MCR_INRQ                (1u << 0)
#define CAN_MCR_SLEEP               (1u << 1)
#define CAN_MCR_TXFP                (1u << 2)
#define CAN_MCR_RFLM                (1u << 3)
#define CAN_MCR_NART                (1u << 4)
#define CAN_MCR_AWUM                (1u << 5)
#define CAN_MCR_ABOM                (1u << 6)
#define CAN_MCR_TTCM                (1u << 7)
#define CAN_MCR_RESET               (1u << 15)
#define CAN_MCR_DBF                 (1u << 16)

#define CAN_MSR_INAK                (1u << 0)
#define CAN_MSR_SLAK                (1u << 1)

#define CAN_TSR_ABRQ0               (1u << 7)
#define CAN_TSR_ABRQ1               (1u << 15)
#define CAN_TSR_ABRQ2               (1u << 23)
#define CAN_TSR_CODE_Pos            24u
#define CAN_TSR_CODE_Msk            (0x3u << 24)
#define CAN_TSR_TME0                (1u << 26)
#define CAN_TSR_TME1                (1u << 27)
#define CAN_TSR_TME2                (1u << 28)
#define CAN_TSR_TME_ALL             (CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2)

#define CAN_RF0R_FMP0_Msk           (0x3u << 0)
#define CAN_RF0R_FULL0              (1u << 3)
#define CAN_RF0R_FOVR0              (1u << 4)
#define CAN_RF0R_RFOM0              (1u << 5)

#define CAN_ESR_EWGF                (1u << 0)
#define CAN_ESR_EPVF                (1u << 1)
#define CAN_ESR_BOFF                (1u << 2)
#define CAN_ESR_LEC_Pos             4u
#define CAN_ESR_TEC_Pos             16u
#define CAN_ESR_REC_Pos             24u

#define CAN_BTR_BRP(x)              ((uint32_t)((x) - 1u) << 0)     /* x = 1..1024 */
#define CAN_BTR_TS1(x)              ((uint32_t)((x) - 1u) << 16)    /* x = 1..16 TQ */
#define CAN_BTR_TS2(x)              ((uint32_t)((x) - 1u) << 20)    /* x = 1..8 TQ */
#define CAN_BTR_SJW(x)              ((uint32_t)((x) - 1u) << 24)    /* x = 1..4 TQ */
#define CAN_BTR_LBKM                (1u << 30)
#define CAN_BTR_SILM                (1u << 31)

/* TIxR / RIxR and 32-bit filter registers share this layout */
#define CAN_ID_TXRQ                 (1u << 0)
#define CAN_ID_RTR                  (1u << 1)
#define CAN_ID_IDE                  (1u << 2)
#define CAN_ID_EXID_Pos             3u      /* 29-bit ID = STID[10:0]:EXID[17:0] at bits 31:3 */

#define CAN_TDTR_DLC_Msk            0xFu
#define CAN_FMR_FINIT               (1u << 0)

/* ---- SysTick (PM0056 4.5) ---- */
#define SYSTICK_CTRL_ENABLE         (1u << 0)
#define SYSTICK_CTRL_TICKINT        (1u << 1)
#define SYSTICK_CTRL_CLKSOURCE      (1u << 2)   /* 1 = processor clock (HCLK) */

/* ---- SCB (PM0056 4.4) ---- */
#define SCB_AIRCR_VECTKEY           (0x05FAu << 16)
#define SCB_AIRCR_PRIGROUP_Msk      (0x7u << 8)
#define SCB_AIRCR_SYSRESETREQ       (1u << 2)
#define SCB_SHCSR_MEMFAULTENA       (1u << 16)
#define SCB_SHCSR_BUSFAULTENA       (1u << 17)
#define SCB_SHCSR_USGFAULTENA       (1u << 18)
#define SCB_CCR_DIV_0_TRP           (1u << 4)
#define DHCSR_C_DEBUGEN             (1u << 0)

/* ---- DBGMCU_CR (RM0008 31.16.3) ---- */
#define DBGMCU_CR_DBG_IWDG_STOP     (1u << 8)
#define DBGMCU_CR_DBG_CAN1_STOP     (1u << 14)

/* ---- IRQ numbers (RM0008 Table 63) ---- */
#define IRQN_USB_HP_CAN_TX          19u
#define IRQN_USB_LP_CAN_RX0         20u
#define IRQN_CAN_RX1                21u
#define IRQN_CAN_SCE                22u
#define IRQN_USART1                 37u

/* Priority: F10x implements 4 priority bits (upper nibble) */
#define NVIC_PRIO(p)                ((uint8_t)((p) << 4))

static inline void nvic_enable_irq(uint32_t irqn, uint8_t prio)
{
    NVIC_IP[irqn] = NVIC_PRIO(prio);
    NVIC_ISER[irqn >> 5] = 1u << (irqn & 0x1Fu);
}

static inline void cpu_enable_irq(void)  { __asm volatile ("cpsie i" ::: "memory"); }
static inline void cpu_disable_irq(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void cpu_dsb(void)         { __asm volatile ("dsb 0xF" ::: "memory"); }

/* =============================================================================
 * COMPILE-TIME LAYOUT CHECKS AGAINST RM0008
 * ============================================================================= */
_Static_assert(offsetof(RCC_TypeDef, APB1ENR) == 0x1Cu, "RCC_APB1ENR offset");
_Static_assert(offsetof(RCC_TypeDef, CSR) == 0x24u, "RCC_CSR offset");
_Static_assert(offsetof(GPIO_TypeDef, BSRR) == 0x10u, "GPIO_BSRR offset");
_Static_assert(offsetof(USART_TypeDef, CR1) == 0x0Cu, "USART_CR1 offset");
_Static_assert(offsetof(CAN_TypeDef, BTR) == 0x01Cu, "CAN_BTR offset");
_Static_assert(offsetof(CAN_TypeDef, TX) == 0x180u, "CAN_TI0R offset");
_Static_assert(offsetof(CAN_TypeDef, RX) == 0x1B0u, "CAN_RI0R offset");
_Static_assert(offsetof(CAN_TypeDef, FMR) == 0x200u, "CAN_FMR offset");
_Static_assert(offsetof(CAN_TypeDef, FS1R) == 0x20Cu, "CAN_FS1R offset");
_Static_assert(offsetof(CAN_TypeDef, FA1R) == 0x21Cu, "CAN_FA1R offset");
_Static_assert(offsetof(CAN_TypeDef, FB) == 0x240u, "CAN_F0R1 offset");
_Static_assert(offsetof(SCB_TypeDef, CFSR) == 0x28u, "SCB_CFSR offset");

#endif /* STM32F103_REG_H */
