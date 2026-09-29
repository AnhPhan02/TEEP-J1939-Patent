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
    HCAN_TX_ERROR   = 2     /* Invalid argument or controller not started */
} hal_can_tx_status_t;

/* Bit timing actually applied by hal_can_init() */
typedef struct {
    uint32_t pclk_hz;       /* Peripheral clock feeding the CAN controller */
    uint32_t prescaler;
    uint8_t  seg1_tq;       /* Time segment 1 (prop + phase1), in TQ */
    uint8_t  seg2_tq;       /* Time segment 2 (phase2), in TQ */
    uint8_t  sjw_tq;        /* Resynchronization jump width, in TQ */
    uint8_t  total_tq;      /* 1 (sync) + seg1 + seg2 */
} hal_can_bit_timing_t;

/* Controller error state snapshot */
typedef struct {
    uint32_t esr;           /* Raw error status register */
    uint8_t  tec;           /* Transmit error counter */
    uint8_t  rec;           /* Receive error counter */
    uint8_t  lec;           /* Last error code (0..7) */
    bool     error_warning; /* TEC or REC >= 96 */
    bool     error_passive; /* TEC or REC > 127 */
    bool     bus_off;       /* TEC > 255, controller off the bus */
} hal_can_error_t;

/* Hardware diagnosis: register snapshot + transceiver echo test */
typedef struct {
    bool     clocked;           /* CAN peripheral clock was enabled */
    uint32_t mcr;               /* Register snapshot taken before the pin test */
    uint32_t msr;
    uint32_t btr;
    uint32_t esr;
    uint32_t afio_mapr;         /* CAN_REMAP in bits 14:13 (SWJ bits read undefined) */
    uint32_t port_cr;           /* GPIO CRL/CRH word holding the CAN pins */
    uint8_t  rx_cfg;            /* CNF:MODE nibble of the RX pin */
    uint8_t  tx_cfg;            /* CNF:MODE nibble of the TX pin */
    bool     rx_high_idle;      /* RX level with TX driven recessive (expect true) */
    bool     rx_low_dominant;   /* RX level went low with TX driven dominant (expect true) */
    bool     rx_high_release;   /* RX back high after TX released (expect true) */
} hal_can_diag_t;

/* Snapshot the CAN registers, then drive the TX pin as a plain GPIO
 * (recessive -> 20 us dominant -> recessive) and read the RX pin back.
 * Bypasses the bxCAN controller entirely, so a failing echo points at the
 * transceiver / wiring. Leaves the controller stopped: call hal_can_init() after. */
void hal_can_diagnose(hal_can_diag_t* out);

/* Initialize (or re-initialize) the controller at the given bitrate and join
 * the bus. Returns false if the bitrate cannot be generated exactly from the
 * peripheral clock or the controller does not acknowledge the mode change. */
bool hal_can_init(uint32_t bitrate_bps);

/* Internal loopback + silent mode self test (TX pin stays recessive, the bus
 * is not disturbed). Leaves the controller stopped; call hal_can_init() after. */
bool hal_can_self_test(uint32_t bitrate_bps);

/* Leave the bus immediately (initialization mode). Safe from fault handlers. */
void hal_can_stop(void);

/* Queue one extended (29-bit) data frame */
hal_can_tx_status_t hal_can_send_ext(uint32_t ext_id, const uint8_t* data, uint8_t len);

/* Fetch one extended frame from RX FIFO0; false if none */
bool hal_can_receive_ext(uint32_t* ext_id, uint8_t* data, uint8_t* len);

void hal_can_get_bit_timing(hal_can_bit_timing_t* out);
void hal_can_get_error(hal_can_error_t* out);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */
