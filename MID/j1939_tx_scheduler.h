/*
 * =============================================================================
 * FILE : j1939_tx_scheduler.h
 * WHAT : Groups active SPNs into PGNs and broadcasts each PGN on its period.
 *        Constant values are packed raw from j1939_value_store; waveform
 *        values come from the pattern pool. Frames go out via j1939_link.
 * =============================================================================
 */

#ifndef J1939_TX_SCHEDULER_H
#define J1939_TX_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_pattern_generator.h"

#define J1939_TX_QUEUE_CAPACITY  32u

typedef enum {
    J1939_SCHED_MODE_SMOOTH = 0,    /* High-fidelity waveform (10-25 ms per PGN) */
    J1939_SCHED_MODE_SAE    = 1,    /* SAE J1939 nominal broadcast periods */
    J1939_SCHED_MODE_STRESS = 2     /* Maximum bus-load stress (5-100 ms) */
} J1939_Sched_Mode_t;

typedef struct {
    uint16_t enqueued;              /* Due frames accepted by the SW queue */
    uint16_t sent;                  /* Frames handed to a hardware mailbox */
    uint16_t busy;                  /* Queue drain paused: all HW mailboxes busy */
    uint16_t failed;                /* Driver errors; frame retained for retry */
    uint16_t dropped;               /* SW queue full */
    uint16_t deadline_missed;       /* One or more periods were skipped */
} J1939_Sched_Tick_Result_t;

typedef struct {
    uint32_t enqueued;
    uint32_t sent;
    uint32_t busy;
    uint32_t failed;
    uint32_t dropped;
    uint32_t deadline_missed;
    uint8_t queued;
    uint8_t high_water;
} J1939_Sched_Stats_t;

/* =============================================================================
 * DEBUG WATCH - read it live with a debugger (Live Watch / ST-LINK memory view).
 * Follows one signal from physical value to the bytes handed to the CAN
 * controller. Write watch_catalog_index from the debugger to follow another
 * generated catalog signal.
 * ============================================================================= */
typedef struct {
    uint16_t watch_catalog_index; /* Defaults to Engine Speed catalog index */
    float    value;             /* Physical value encoded for watched signal */
    float    decoded;           /* Value decoded back from the payload (round-trip check) */
    uint32_t can_id;            /* 29-bit ID of the frame that carried it */
    uint8_t  data[8];           /* Payload exactly as handed to the CAN controller */
    uint8_t  link_status;       /* 0 = OK (queued), 1 = BUSY (mailboxes full), 2 = ERROR */
    uint32_t timestamp_ms;      /* hal_time_ms() when the frame was queued */
    uint32_t tx_ok;             /* All frames queued since boot */
    uint32_t tx_busy;           /* All frames dropped: mailboxes full */
    uint32_t tx_error;          /* All frames dropped: driver error */
} J1939_Debug_Watch_t;

extern volatile J1939_Debug_Watch_t g_j1939_dbg;

/* Remove all signals and PGNs */
void J1939_Sched_Clear(void);

/* Native Phase-3 API. Only the owning PGN is stored; signal metadata is read
 * directly from its contiguous generated-catalog range. timeframe_ms is kept
 * for protocol compatibility, but generated approved timing is authoritative. */
bool J1939_Sched_Can_Add_Catalog_Signal(uint16_t catalog_index);
bool J1939_Sched_Add_Catalog_Signal(uint16_t catalog_index,
                                    uint32_t timeframe_ms,
                                    uint32_t now_ms);

void J1939_Sched_Set_Mode(J1939_Sched_Mode_t mode);
J1939_Sched_Mode_t J1939_Sched_Get_Mode(void);

/* Restart all PGN period timers from now_ms */
void J1939_Sched_Reset_Timers(uint32_t now_ms);

/* Encode and transmit every PGN whose period has elapsed */
void J1939_Sched_Tick(uint32_t now_ms, J1939_Sched_Tick_Result_t* result);

uint16_t J1939_Sched_Get_Signal_Count(void);
bool J1939_Sched_Get_Active_Catalog_Index(uint16_t ordinal,
                                          uint16_t* out_catalog_index);
uint8_t J1939_Sched_Get_PGN_Count(void);
void J1939_Sched_Get_Stats(J1939_Sched_Stats_t* stats);

/* $TX frame log on the console (see j1939_tx_scheduler.c for the format) */
void     J1939_Sched_Set_Tx_Log(bool enable);
bool     J1939_Sched_Get_Tx_Log(void);
uint32_t J1939_Sched_Get_Tx_Log_Dropped(void);  /* lines skipped: UART too slow */
uint32_t J1939_Sched_Get_Tx_Seq(void);          /* send attempts since boot */

#ifdef __cplusplus
}
#endif
#endif /* J1939_TX_SCHEDULER_H */
