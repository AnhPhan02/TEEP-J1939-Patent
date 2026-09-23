/*
 * =============================================================================
 * FILE : j1939_tx_scheduler.h
 * WHAT : Groups active SPNs into PGNs and broadcasts each PGN on its period.
 *        Values come from the pattern generator, frames go out via j1939_link.
 * =============================================================================
 */

#ifndef J1939_TX_SCHEDULER_H
#define J1939_TX_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_signal_definitions.h"
#include "j1939_pattern_generator.h"

#define J1939_SCHED_MAX_SIGNALS  PATTERN_GEN_MAX_SIGNALS
#define J1939_SCHED_MAX_PGNS     35u

typedef enum {
    J1939_SCHED_MODE_SMOOTH = 0,    /* High-fidelity waveform (10-25 ms per PGN) */
    J1939_SCHED_MODE_SAE    = 1,    /* SAE J1939 nominal broadcast periods */
    J1939_SCHED_MODE_STRESS = 2     /* Maximum bus-load stress (5-100 ms) */
} J1939_Sched_Mode_t;

typedef struct {
    uint32_t spn;
    uint32_t pgn;
    const J1939_Signal_Definition_t* def;
    uint32_t timeframe_ms;          /* 0 = use PGN period of current mode */
    float    t_start;
    float    t_dur;
} J1939_Sched_Signal_t;

typedef struct {
    uint16_t sent;                  /* Frames queued successfully */
    uint16_t busy;                  /* Frames dropped: controller mailboxes full */
    uint16_t failed;                /* Frames dropped: driver error */
} J1939_Sched_Tick_Result_t;

/* Remove all signals and PGNs */
void J1939_Sched_Clear(void);

/* Add or update a signal; its PGN is tracked automatically.
 * A non-zero timeframe_ms overrides the PGN period if it is faster. */
bool J1939_Sched_Add_Signal(const J1939_Signal_Definition_t* def,
                            uint32_t timeframe_ms,
                            float t_start,
                            float t_dur,
                            uint32_t now_ms);

void J1939_Sched_Set_Mode(J1939_Sched_Mode_t mode);
J1939_Sched_Mode_t J1939_Sched_Get_Mode(void);

/* Restart all PGN period timers from now_ms */
void J1939_Sched_Reset_Timers(uint32_t now_ms);

/* Encode and transmit every PGN whose period has elapsed */
void J1939_Sched_Tick(uint32_t now_ms, J1939_Sched_Tick_Result_t* result);

uint8_t J1939_Sched_Get_Signal_Count(void);
const J1939_Sched_Signal_t* J1939_Sched_Get_Signal(uint8_t index);
uint8_t J1939_Sched_Get_PGN_Count(void);

#ifdef __cplusplus
}
#endif
#endif /* J1939_TX_SCHEDULER_H */
