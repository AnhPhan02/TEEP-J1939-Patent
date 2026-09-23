/*
 * =============================================================================
 * FILE : hal_can.h
 * WHAT : Hardware-neutral CAN driver interface (raw 29-bit extended frames).
 *        Knows nothing about J1939 - protocol framing lives in MID.
 * =============================================================================
 */

#ifndef HAL_CAN_H
#define HAL_CAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HCAN_TX_OK      = 0,    /* Frame queued in a hardware mailbox */
    HCAN_TX_TIMEOUT = 1,    /* No free mailbox in time, pending frames aborted */
    HCAN_TX_ERROR   = 2     /* Invalid argument or driver error */
} hal_can_tx_status_t;

/* Bit timing actually applied by hal_can_init() */
typedef struct {
    uint32_t pclk_hz;       /* Peripheral clock feeding the CAN controller */
    uint32_t prescaler;
    uint8_t  seg1_tq;       /* Time segment 1 (prop + phase1), in TQ */
    uint8_t  seg2_tq;       /* Time segment 2 (phase2), in TQ */
    uint8_t  total_tq;      /* 1 (sync) + seg1 + seg2 */
} hal_can_bit_timing_t;

/* Controller error state snapshot */
typedef struct {
    uint32_t esr;           /* Raw error status register */
    uint8_t  tec;           /* Transmit error counter */
    uint8_t  rec;           /* Receive error counter */
    uint8_t  lec;           /* Last error code (0..7) */
} hal_can_error_t;

/* Initialize (or re-initialize) the controller at the given bitrate */
bool hal_can_init(uint32_t bitrate_bps);

/* Queue one extended (29-bit) data frame */
hal_can_tx_status_t hal_can_send_ext(uint32_t ext_id, const uint8_t* data, uint8_t len);

/* Fetch one extended frame from RX FIFO; false if none (standard frames are dropped) */
bool hal_can_receive_ext(uint32_t* ext_id, uint8_t* data, uint8_t* len);

void hal_can_get_bit_timing(hal_can_bit_timing_t* out);
void hal_can_get_error(hal_can_error_t* out);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */
