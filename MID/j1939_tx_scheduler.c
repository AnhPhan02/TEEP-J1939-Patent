/*
 * =============================================================================
 * FILE : j1939_tx_scheduler.c
 * WHAT : Periodic PGN broadcast scheduler.
 * =============================================================================
 */

#include "j1939_tx_scheduler.h"
#include "j1939_pgn_timing.h"
#include "j1939_encode_decode.h"
#include "j1939_link.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    J1939_PGN_Timing_t timing;
    uint32_t custom_ms;             /* 0 = use period of current mode */
    uint32_t last_tx_ms;
} Sched_PGN_Entry_t;

static J1939_Sched_Signal_t s_signals[J1939_SCHED_MAX_SIGNALS];
static uint8_t              s_signal_count = 0u;

static Sched_PGN_Entry_t    s_pgns[J1939_SCHED_MAX_PGNS];
static uint8_t              s_pgn_count = 0u;

static J1939_Sched_Mode_t   s_mode = J1939_SCHED_MODE_SMOOTH;

/* =============================================================================
 * PRIVATE
 * ============================================================================= */
static Sched_PGN_Entry_t* prv_find_pgn(uint32_t pgn)
{
    for (uint8_t i = 0; i < s_pgn_count; i++) {
        if (s_pgns[i].timing.pgn == pgn) {
            return &s_pgns[i];
        }
    }
    return NULL;
}

static Sched_PGN_Entry_t* prv_ensure_pgn(uint32_t pgn, uint32_t now_ms)
{
    Sched_PGN_Entry_t* entry = prv_find_pgn(pgn);
    if (entry == NULL && s_pgn_count < J1939_SCHED_MAX_PGNS) {
        entry = &s_pgns[s_pgn_count++];
        J1939_PGN_Get_Timing(pgn, &entry->timing);
        entry->custom_ms = 0u;
        entry->last_tx_ms = now_ms;
    }
    return entry;
}

static uint32_t prv_period_ms(const Sched_PGN_Entry_t* entry)
{
    if (entry->custom_ms > 0u) {
        return entry->custom_ms;
    }
    switch (s_mode) {
        case J1939_SCHED_MODE_SAE:    return entry->timing.sae_ms;
        case J1939_SCHED_MODE_STRESS: return entry->timing.stress_ms;
        default:                      return entry->timing.smooth_ms;
    }
}

/* =============================================================================
 * PUBLIC
 * ============================================================================= */
void J1939_Sched_Clear(void)
{
    s_signal_count = 0u;
    s_pgn_count = 0u;
}

bool J1939_Sched_Add_Signal(const J1939_Signal_Definition_t* def,
                            uint32_t timeframe_ms,
                            float t_start,
                            float t_dur,
                            uint32_t now_ms)
{
    if (def == NULL) {
        return false;
    }

    J1939_Sched_Signal_t* sig = NULL;
    for (uint8_t i = 0; i < s_signal_count; i++) {
        if (s_signals[i].spn == def->spn) {
            sig = &s_signals[i];
            break;
        }
    }
    if (sig == NULL) {
        if (s_signal_count >= J1939_SCHED_MAX_SIGNALS) {
            return false;
        }
        sig = &s_signals[s_signal_count++];
    }

    sig->spn = def->spn;
    sig->pgn = def->pgn;
    sig->def = def;
    sig->timeframe_ms = timeframe_ms;
    sig->t_start = t_start;
    sig->t_dur = t_dur;

    Sched_PGN_Entry_t* entry = prv_ensure_pgn(def->pgn, now_ms);
    if (entry == NULL) {
        return false;
    }
    if (timeframe_ms > 0u && (entry->custom_ms == 0u || timeframe_ms < entry->custom_ms)) {
        entry->custom_ms = timeframe_ms;
    }
    return true;
}

void J1939_Sched_Set_Mode(J1939_Sched_Mode_t mode)
{
    s_mode = mode;
}

J1939_Sched_Mode_t J1939_Sched_Get_Mode(void)
{
    return s_mode;
}

void J1939_Sched_Reset_Timers(uint32_t now_ms)
{
    for (uint8_t i = 0; i < s_pgn_count; i++) {
        s_pgns[i].last_tx_ms = now_ms;
    }
}

void J1939_Sched_Tick(uint32_t now_ms, J1939_Sched_Tick_Result_t* result)
{
    J1939_Sched_Tick_Result_t local;
    memset(&local, 0, sizeof(local));

    for (uint8_t i = 0; i < s_pgn_count; i++) {
        Sched_PGN_Entry_t* entry = &s_pgns[i];

        if (now_ms - entry->last_tx_ms < prv_period_ms(entry)) {
            continue;
        }
        entry->last_tx_ms = now_ms;

        /* Unassigned bytes stay 0xFF ("not available") */
        uint8_t payload[8];
        J1939_Initialize_Payload(payload);

        for (uint8_t s = 0; s < s_signal_count; s++) {
            const J1939_Sched_Signal_t* sig = &s_signals[s];
            if (sig->pgn == entry->timing.pgn) {
                float value = Pattern_Generator_Get_Value_Instant(sig->spn, now_ms, sig->def->min_physical);
                J1939_Encode_Signal(sig->def, value, payload);
            }
        }

        switch (J1939_Link_Send(entry->timing.pgn, payload, 8u,
                                entry->timing.priority, entry->timing.source_address)) {
            case J1939_LINK_OK:   local.sent++;   break;
            case J1939_LINK_BUSY: local.busy++;   break;
            default:              local.failed++; break;
        }
    }

    if (result != NULL) {
        *result = local;
    }
}

uint8_t J1939_Sched_Get_Signal_Count(void)
{
    return s_signal_count;
}

const J1939_Sched_Signal_t* J1939_Sched_Get_Signal(uint8_t index)
{
    return (index < s_signal_count) ? &s_signals[index] : NULL;
}

uint8_t J1939_Sched_Get_PGN_Count(void)
{
    return s_pgn_count;
}
