/*
 * =============================================================================
 * FILE : hal_can_stm32f1.c
 * WHAT : hal_can.h port - register-level bxCAN driver (RM0008 section 24).
 *
 * Init sequence (RM0008 24.4.1 / 24.7):
 *   1. Clock gate: RCC_APB2ENR (GPIOB, AFIO), RCC_APB1ENR (CAN); pulse CANRST
 *   2. Pins     : AFIO_MAPR CAN_REMAP=10 -> RX=PB8, TX=PB9
 *   3. Mode     : MCR.SLEEP=0, MCR.INRQ=1, wait MSR.INAK=1 / SLAK=0
 *   4. Options  : ABOM=1 (auto bus-off recovery), NART=0, TXFP=0 (ID priority)
 *   5. BTR      : BRP / TS1 / TS2 / SJW (+ LBKM/SILM for self test)
 *   6. Filter   : bank 0, 32-bit mask, accept extended data frames only
 *   7. Normal   : MCR.INRQ=0, wait MSR.INAK=0 (11 recessive bits seen)
 * =============================================================================
 */

#include "hal_can.h"
#include "hal_system.h"
#include "hal_time.h"
#include "hal_board_cfg.h"

#define CAN_SJW_TQ          1u
#define CAN_SELF_TEST_ID    0x18FEF1FEu
#define CAN_SELF_TEST_MS    5u

static hal_can_bit_timing_t s_timing;
static bool                 s_started = false;

/* Exact presets for the two clock plans in hal_board_cfg.h.
 * Target sample point 87.5 % (CiA 601 / J1939 recommendation). */
typedef struct {
    uint32_t pclk_hz;
    uint32_t bitrate_bps;
    uint16_t prescaler;
    uint8_t  seg1_tq;
    uint8_t  seg2_tq;
} prv_timing_preset_t;

static const prv_timing_preset_t k_presets[] = {
    { 36000000u, 250000u, 9u, 13u, 2u },    /* 16 TQ, SP 87.5 % */
    { 36000000u, 500000u, 4u, 15u, 2u },    /* 18 TQ, SP 88.9 % */
    { 32000000u, 250000u, 8u, 13u, 2u },    /* 16 TQ, SP 87.5 % */
    { 32000000u, 500000u, 4u, 13u, 2u },    /* 16 TQ, SP 87.5 % */
};

/* =============================================================================
 * PRIVATE
 * ============================================================================= */
static bool prv_calc_bit_timing(uint32_t pclk, uint32_t bitrate, hal_can_bit_timing_t* t)
{
    t->pclk_hz = pclk;
    t->sjw_tq = CAN_SJW_TQ;

    for (uint32_t i = 0; i < sizeof(k_presets) / sizeof(k_presets[0]); i++) {
        if (k_presets[i].pclk_hz == pclk && k_presets[i].bitrate_bps == bitrate) {
            t->prescaler = k_presets[i].prescaler;
            t->seg1_tq = k_presets[i].seg1_tq;
            t->seg2_tq = k_presets[i].seg2_tq;
            t->total_tq = (uint8_t)(1u + t->seg1_tq + t->seg2_tq);
            return true;
        }
    }

    /* Generic: largest TQ count (8..25) dividing the clock exactly, SP ~87.5 % */
    if (bitrate == 0u) {
        return false;
    }
    for (uint32_t tq = 25u; tq >= 8u; tq--) {
        if ((pclk % (tq * bitrate)) != 0u) {
            continue;
        }
        uint32_t seg2 = (tq + 4u) / 8u;                     /* round(tq * 12.5 %) */
        if (seg2 < 1u) seg2 = 1u;
        uint32_t seg1 = tq - 1u - seg2;
        uint32_t brp = pclk / (tq * bitrate);
        if (seg1 > 16u || seg2 > 8u || brp > 1024u) {
            continue;
        }
        t->prescaler = brp;
        t->seg1_tq = (uint8_t)seg1;
        t->seg2_tq = (uint8_t)seg2;
        t->total_tq = (uint8_t)tq;
        return true;
    }
    return false;                                           /* no exact setting */
}

static bool prv_wait_msr(uint32_t mask, uint32_t expected)
{
    for (uint32_t i = 0; i < BOARD_CAN_MODE_LOOPS; i++) {
        if ((CAN1->MSR & mask) == expected) {
            return true;
        }
    }
    return false;
}

static bool prv_enter_init_mode(void)
{
    CAN1->MCR = (CAN1->MCR & ~CAN_MCR_SLEEP) | CAN_MCR_INRQ;
    return prv_wait_msr(CAN_MSR_INAK | CAN_MSR_SLAK, CAN_MSR_INAK);
}

static void prv_clock_and_pins(void)
{
    /* 1. Clock gates, then a clean peripheral reset */
    RCC->APB2ENR |= BOARD_CAN_PORT_CLK | RCC_APB2_AFIO;
    RCC->APB1ENR |= RCC_APB1_CAN;
    (void)RCC->APB1ENR;                                     /* settle before first access */
    RCC->APB1RSTR |= RCC_APB1_CAN;
    RCC->APB1RSTR &= ~RCC_APB1_CAN;

    /* 2. Remap. SWJ_CFG is write-only (reads undefined): always write it explicitly */
    uint32_t mapr = AFIO->MAPR & ~(AFIO_MAPR_CAN_REMAP_Msk | AFIO_MAPR_SWJ_CFG_Msk);
    AFIO->MAPR = mapr | BOARD_CAN_REMAP | BOARD_SWJ_CFG;

    /* TX latch recessive (high) before switching the pin to AF, avoids a glitch */
    BOARD_CAN_PORT->BSRR = (1u << BOARD_CAN_TX_PIN) | (1u << BOARD_CAN_RX_PIN);
    board_gpio_config(BOARD_CAN_PORT, BOARD_CAN_RX_PIN, GPIO_CFG_INPUT_PULL);    /* pull-up */
    board_gpio_config(BOARD_CAN_PORT, BOARD_CAN_TX_PIN, GPIO_CFG_AF_PP_50MHZ);
}

static void prv_config_filters(void)
{
    /* Bank 0, 32-bit mask mode -> FIFO0: IDE must be 1, RTR must be 0.
     * Standard-ID and remote frames are rejected in hardware (J1939 uses neither). */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1u;
    CAN1->FS1R |= 1u;
    CAN1->FM1R &= ~1u;
    CAN1->FFA1R &= ~1u;
    CAN1->FB[0].FR1 = CAN_ID_IDE;
    CAN1->FB[0].FR2 = CAN_ID_IDE | CAN_ID_RTR;
    CAN1->FA1R |= 1u;
    CAN1->FMR &= ~CAN_FMR_FINIT;
}

static bool prv_configure(uint32_t bitrate_bps, uint32_t btr_test_bits)
{
    hal_clock_info_t clk;
    hal_can_bit_timing_t timing;

    s_started = false;
    hal_system_get_clocks(&clk);
    if (!prv_calc_bit_timing(clk.pclk1_hz, bitrate_bps, &timing)) {
        return false;
    }
    s_timing = timing;

    prv_clock_and_pins();

    /* 3. Wake up + initialization mode */
    if (!prv_enter_init_mode()) {
        return false;
    }

    /* 4. Operating options (DBF: freeze while the core is halted by a debugger) */
    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_ABOM | CAN_MCR_DBF;

    /* 5. Bit timing (writable only in initialization mode) */
    CAN1->BTR = btr_test_bits |
                CAN_BTR_SJW(timing.sjw_tq) |
                CAN_BTR_TS2(timing.seg2_tq) |
                CAN_BTR_TS1(timing.seg1_tq) |
                CAN_BTR_BRP(timing.prescaler);

    /* 6. Acceptance filter */
    prv_config_filters();

    /* Polling driver: no CAN interrupts yet */
    CAN1->IER = 0u;

    /* 7. Normal mode - needs 11 recessive bits on RX to synchronize */
    CAN1->MCR &= ~CAN_MCR_INRQ;
    if (!prv_wait_msr(CAN_MSR_INAK, 0u)) {
        return false;
    }

    s_started = true;
    return true;
}

/* =============================================================================
 * PUBLIC
 * ============================================================================= */
bool hal_can_init(uint32_t bitrate_bps)
{
    return prv_configure(bitrate_bps, 0u);
}

bool hal_can_self_test(uint32_t bitrate_bps)
{
    if (!prv_configure(bitrate_bps, CAN_BTR_LBKM | CAN_BTR_SILM)) {
        return false;
    }

    static const uint8_t k_pattern[8] = { 0x55, 0xAA, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB };
    bool ok = (hal_can_send_ext(CAN_SELF_TEST_ID, k_pattern, 8u) == HCAN_TX_OK);

    if (ok) {
        uint32_t id = 0u;
        uint8_t data[8] = {0};
        uint8_t len = 0u;
        uint32_t start = hal_time_ms();

        ok = false;
        while ((hal_time_ms() - start) <= CAN_SELF_TEST_MS) {
            if (hal_can_receive_ext(&id, data, &len)) {
                ok = (id == CAN_SELF_TEST_ID) && (len == 8u);
                for (uint8_t i = 0; ok && i < 8u; i++) {
                    ok = (data[i] == k_pattern[i]);
                }
                break;
            }
        }
    }

    hal_can_stop();
    return ok;
}

void hal_can_stop(void)
{
    s_started = false;
    if ((RCC->APB1ENR & RCC_APB1_CAN) == 0u) {
        return;                                             /* never clocked */
    }
    CAN1->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;
    (void)prv_enter_init_mode();
}

hal_can_tx_status_t hal_can_send_ext(uint32_t ext_id, const uint8_t* data, uint8_t len)
{
    if (!s_started || data == NULL || len > 8u) {
        return HCAN_TX_ERROR;
    }

    uint32_t start = hal_time_ms();
    while ((CAN1->TSR & CAN_TSR_TME_ALL) == 0u) {
        if ((hal_time_ms() - start) > BOARD_CAN_TX_TIMEOUT_MS) {
            /* Nobody ACKs / bus overloaded: drop pending frames, keep the scheduler alive */
            CAN1->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;
            return HCAN_TX_TIMEOUT;
        }
    }

    uint32_t mb = (CAN1->TSR & CAN_TSR_CODE_Msk) >> CAN_TSR_CODE_Pos;   /* next empty mailbox */
    uint8_t b[8] = {0};
    for (uint8_t i = 0; i < len; i++) {
        b[i] = data[i];
    }

    CAN1->TX[mb].TDTR = len;
    CAN1->TX[mb].TDLR = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    CAN1->TX[mb].TDHR = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    CAN1->TX[mb].TIR = ((ext_id & 0x1FFFFFFFu) << CAN_ID_EXID_Pos) | CAN_ID_IDE | CAN_ID_TXRQ;

    return HCAN_TX_OK;
}

bool hal_can_receive_ext(uint32_t* ext_id, uint8_t* data, uint8_t* len)
{
    if (ext_id == NULL || data == NULL || len == NULL) {
        return false;
    }
    if ((CAN1->RF0R & CAN_RF0R_FMP0_Msk) == 0u) {
        return false;
    }

    uint32_t rir = CAN1->RX[0].RIR;
    uint32_t dlc = CAN1->RX[0].RDTR & CAN_TDTR_DLC_Msk;
    uint32_t lo = CAN1->RX[0].RDLR;
    uint32_t hi = CAN1->RX[0].RDHR;
    CAN1->RF0R = CAN_RF0R_RFOM0;                            /* release the output mailbox */

    if ((rir & CAN_ID_IDE) == 0u) {
        return false;
    }
    if (dlc > 8u) {
        dlc = 8u;
    }

    *ext_id = rir >> CAN_ID_EXID_Pos;
    *len = (uint8_t)dlc;
    for (uint32_t i = 0; i < dlc; i++) {
        data[i] = (uint8_t)(((i < 4u) ? lo : hi) >> (8u * (i & 3u)));
    }
    return true;
}

void hal_can_get_bit_timing(hal_can_bit_timing_t* out)
{
    if (out != NULL) {
        *out = s_timing;
    }
}

void hal_can_get_error(hal_can_error_t* out)
{
    if (out == NULL) {
        return;
    }
    uint32_t esr = ((RCC->APB1ENR & RCC_APB1_CAN) != 0u) ? CAN1->ESR : 0u;
    out->esr = esr;
    out->tec = (uint8_t)(esr >> CAN_ESR_TEC_Pos);
    out->rec = (uint8_t)(esr >> CAN_ESR_REC_Pos);
    out->lec = (uint8_t)((esr >> CAN_ESR_LEC_Pos) & 0x7u);
    out->error_warning = (esr & CAN_ESR_EWGF) != 0u;
    out->error_passive = (esr & CAN_ESR_EPVF) != 0u;
    out->bus_off = (esr & CAN_ESR_BOFF) != 0u;
}
