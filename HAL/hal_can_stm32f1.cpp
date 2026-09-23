/*
 * =============================================================================
 * FILE : hal_can_stm32f1.cpp
 * WHAT : hal_can.h port for STM32F1 bxCAN (STM32duino core + ST HAL).
 * =============================================================================
 */

#include "hal_can.h"
#include <Arduino.h>            /* Pulls in stm32f1xx_hal.h via the core */
#include "hal_board_cfg.h"

static CAN_HandleTypeDef    s_hcan;
static hal_can_bit_timing_t s_timing;

/* Known-good presets (~80-87% sample point) for common APB1 clocks */
typedef struct {
    uint32_t pclk_hz;
    uint32_t bitrate_bps;
    uint16_t prescaler;
    uint8_t  seg1_tq;
    uint8_t  seg2_tq;
} prv_timing_preset_t;

static const prv_timing_preset_t k_timing_presets[] = {
    { 36000000u, 500000u, 4u, 14u, 3u },    /* 18 TQ, SP 83.33% */
    { 32000000u, 500000u, 4u, 12u, 3u },    /* 16 TQ, SP 81.25% */
    { 18000000u, 500000u, 2u, 14u, 3u },    /* 18 TQ, SP 83.33% */
    {  8000000u, 500000u, 1u, 12u, 3u },    /* 16 TQ, SP 81.25% */
    { 36000000u, 250000u, 9u, 12u, 3u },    /* 16 TQ, SP 81.25% */
    { 32000000u, 250000u, 8u, 12u, 3u },    /* 16 TQ, SP 81.25% */
    { 18000000u, 250000u, 4u, 13u, 4u },    /* 18 TQ, SP 77.78% */
    {  8000000u, 250000u, 2u, 12u, 3u },    /* 16 TQ, SP 81.25% */
};

/* CAN MSP Initialization (overrides the weak HAL function) */
extern "C" void HAL_CAN_MspInit(CAN_HandleTypeDef* hcan_inst)
{
    if (hcan_inst->Instance == BOARD_CAN_INSTANCE) {
        BOARD_CAN_CLK_ENABLE();
        BOARD_CAN_GPIO_CLK_ENABLE();
        __HAL_RCC_AFIO_CLK_ENABLE();

        /* Remap CAN pins FIRST before initializing GPIO pins */
        BOARD_CAN_PIN_REMAP();

        /* Pre-set TX HIGH (recessive) to prevent dominant glitch during startup */
        HAL_GPIO_WritePin(BOARD_CAN_TX_PORT, BOARD_CAN_TX_PIN, GPIO_PIN_SET);

        GPIO_InitTypeDef GPIO_InitStruct = {0};

        GPIO_InitStruct.Pin = BOARD_CAN_TX_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(BOARD_CAN_TX_PORT, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = BOARD_CAN_RX_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(BOARD_CAN_RX_PORT, &GPIO_InitStruct);
    }
}

/* CAN MSP De-Initialization (overrides the weak HAL function) */
extern "C" void HAL_CAN_MspDeInit(CAN_HandleTypeDef* hcan_inst)
{
    if (hcan_inst->Instance == BOARD_CAN_INSTANCE) {
        BOARD_CAN_CLK_DISABLE();
        HAL_GPIO_DeInit(BOARD_CAN_TX_PORT, BOARD_CAN_TX_PIN);
        HAL_GPIO_DeInit(BOARD_CAN_RX_PORT, BOARD_CAN_RX_PIN);
    }
}

/* =============================================================================
 * PRIVATE: Pick prescaler/segments for the requested bitrate
 * ============================================================================= */
static void prv_calc_bit_timing(uint32_t pclk, uint32_t bitrate, hal_can_bit_timing_t* t)
{
    t->pclk_hz = pclk;

    for (uint32_t i = 0; i < sizeof(k_timing_presets) / sizeof(k_timing_presets[0]); i++) {
        const prv_timing_preset_t* p = &k_timing_presets[i];
        if (p->pclk_hz == pclk && p->bitrate_bps == bitrate) {
            t->prescaler = p->prescaler;
            t->seg1_tq = p->seg1_tq;
            t->seg2_tq = p->seg2_tq;
            t->total_tq = (uint8_t)(1u + p->seg1_tq + p->seg2_tq);
            return;
        }
    }

    /* Generic search: largest TQ count that divides the clock exactly, SP ~80% */
    const uint32_t max_tq = (bitrate >= 500000u) ? 18u : 16u;
    for (uint32_t tq = max_tq; tq >= 8u; tq--) {
        if ((pclk % (tq * bitrate)) == 0u) {
            uint32_t seg2 = tq / 5u;
            if (seg2 < 1u) seg2 = 1u;
            t->prescaler = pclk / (tq * bitrate);
            t->seg2_tq = (uint8_t)seg2;
            t->seg1_tq = (uint8_t)(tq - 1u - seg2);
            t->total_tq = (uint8_t)tq;
            return;
        }
    }

    /* Fallback: nearest prescaler, bitrate will be approximate */
    t->prescaler = pclk / (max_tq * bitrate);
    if (t->prescaler == 0u) t->prescaler = 1u;
    t->seg1_tq = (max_tq == 18u) ? 14u : 12u;
    t->seg2_tq = 3u;
    t->total_tq = (uint8_t)max_tq;
}

bool hal_can_init(uint32_t bitrate_bps)
{
    /* If peripheral was already running, stop it first */
    HAL_CAN_Stop(&s_hcan);

    s_hcan.Instance = BOARD_CAN_INSTANCE;

    prv_calc_bit_timing(HAL_RCC_GetPCLK1Freq(), bitrate_bps, &s_timing);

    s_hcan.Init.Prescaler = s_timing.prescaler;
    s_hcan.Init.Mode = CAN_MODE_NORMAL;
    s_hcan.Init.SyncJumpWidth = CAN_SJW_2TQ;
    s_hcan.Init.TimeSeg1 = (uint32_t)(s_timing.seg1_tq - 1u) << CAN_BTR_TS1_Pos;
    s_hcan.Init.TimeSeg2 = (uint32_t)(s_timing.seg2_tq - 1u) << CAN_BTR_TS2_Pos;
    s_hcan.Init.TimeTriggeredMode = DISABLE;
    s_hcan.Init.AutoBusOff = ENABLE;
    s_hcan.Init.AutoWakeUp = DISABLE;
    s_hcan.Init.AutoRetransmission = ENABLE;
    s_hcan.Init.ReceiveFifoLocked = DISABLE;
    s_hcan.Init.TransmitFifoPriority = DISABLE;

    if (HAL_CAN_Init(&s_hcan) != HAL_OK) {
        return false;
    }

    /* Accept-all filter into FIFO0 */
    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&s_hcan, &sFilterConfig) != HAL_OK) {
        return false;
    }

    return (HAL_CAN_Start(&s_hcan) == HAL_OK);
}

hal_can_tx_status_t hal_can_send_ext(uint32_t ext_id, const uint8_t* data, uint8_t len)
{
    if (data == NULL || len > 8u) {
        return HCAN_TX_ERROR;
    }

    /* Wait for a free hardware Tx mailbox */
    uint32_t start_wait = HAL_GetTick();
    while (HAL_CAN_GetTxMailboxesFreeLevel(&s_hcan) == 0u) {
        if (HAL_GetTick() - start_wait > BOARD_CAN_TX_TIMEOUT_MS) {
            /* Abort stuck frames so hardware mailboxes don't deadlock */
            HAL_CAN_AbortTxRequest(&s_hcan, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
            return HCAN_TX_TIMEOUT;
        }
    }

    CAN_TxHeaderTypeDef tx_header;
    uint32_t mailbox;

    tx_header.StdId = 0;
    tx_header.ExtId = ext_id & 0x1FFFFFFFu;
    tx_header.IDE = CAN_ID_EXT;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = len;
    tx_header.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(&s_hcan, &tx_header, (uint8_t*)data, &mailbox) != HAL_OK) {
        return HCAN_TX_ERROR;
    }

    return HCAN_TX_OK;
}

bool hal_can_receive_ext(uint32_t* ext_id, uint8_t* data, uint8_t* len)
{
    if (ext_id == NULL || data == NULL || len == NULL) {
        return false;
    }

    if (HAL_CAN_GetRxFifoFillLevel(&s_hcan, CAN_RX_FIFO0) == 0u) {
        return false;
    }

    CAN_RxHeaderTypeDef rx_header;
    if (HAL_CAN_GetRxMessage(&s_hcan, CAN_RX_FIFO0, &rx_header, data) != HAL_OK) {
        return false;
    }

    if (rx_header.IDE != CAN_ID_EXT) {
        return false;
    }

    *ext_id = rx_header.ExtId;
    *len = (uint8_t)rx_header.DLC;
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
    uint32_t esr = (s_hcan.Instance != NULL) ? s_hcan.Instance->ESR : 0u;
    out->esr = esr;
    out->tec = (uint8_t)((esr >> 16) & 0xFFu);
    out->rec = (uint8_t)((esr >> 24) & 0xFFu);
    out->lec = (uint8_t)((esr >> 4) & 0x07u);
}
