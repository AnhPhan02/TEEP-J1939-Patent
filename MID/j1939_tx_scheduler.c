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
#include "hal_console.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    J1939_PGN_Timing_t timing;
    uint32_t custom_ms;             /* 0 = use period of current mode */
    uint32_t first_start_ms;
    uint64_t next_due_ms;
} Sched_PGN_Entry_t;

static J1939_Sched_Signal_t s_signals[J1939_SCHED_MAX_SIGNALS];
static uint8_t              s_signal_count = 0u;

static Sched_PGN_Entry_t    s_pgns[J1939_SCHED_MAX_PGNS];
static uint8_t              s_pgn_count = 0u;

static uint32_t s_last_time_ms;
static uint64_t s_elapsed_ms;
static J1939_Sched_Stats_t s_stats;

static J1939_Sched_Mode_t   s_mode = J1939_SCHED_MODE_SMOOTH;

volatile J1939_Debug_Watch_t g_j1939_dbg = { .watch_spn = SPN_ENGINE_SPEED };

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
        entry->first_start_ms = UINT32_MAX;
        entry->next_due_ms = 0u;
        (void)now_ms;
    }
    return entry;
}

static void prv_debug_capture(const Sched_PGN_Entry_t* entry,
                              const J1939_Signal_Definition_t* def,
                              float value,
                              const uint8_t* payload,
                              J1939_Link_Status_t status,
                              uint32_t now_ms)
{
    J1939_Frame_Header_t hdr;
    hdr.pgn = entry->timing.pgn;
    hdr.priority = entry->timing.priority;
    hdr.source_address = entry->timing.source_address;
    hdr.destination_address = J1939_ADDR_GLOBAL;

    g_j1939_dbg.value = value;
    g_j1939_dbg.decoded = J1939_Decode_Signal(def, payload);
    g_j1939_dbg.can_id = J1939_Frame_Build_Id(&hdr);
    for (uint8_t i = 0; i < 8u; i++) {
        g_j1939_dbg.data[i] = payload[i];
    }
    g_j1939_dbg.link_status = (uint8_t)status;
    g_j1939_dbg.timestamp_ms = now_ms;
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
    memset(&s_stats, 0, sizeof(s_stats));
}

bool J1939_Sched_Add_Signal(const J1939_Signal_Definition_t* def,
                            uint32_t timeframe_ms,
                            uint32_t start_ms,
                            uint32_t duration_ms,
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
    sig->start_ms = start_ms;
    sig->duration_ms = duration_ms;

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
    s_last_time_ms = now_ms;
    s_elapsed_ms = 0u;
    memset(&s_stats, 0, sizeof(s_stats));
    for (uint8_t i = 0; i < s_pgn_count; i++) {
        Sched_PGN_Entry_t* entry = &s_pgns[i];
        entry->first_start_ms = UINT32_MAX;
        entry->custom_ms = 0u;
        for (uint8_t j = 0; j < s_signal_count; j++) {
            const J1939_Sched_Signal_t* sig = &s_signals[j];
            if (sig->pgn != entry->timing.pgn) continue;
            if (sig->start_ms < entry->first_start_ms) entry->first_start_ms = sig->start_ms;
            if (sig->timeframe_ms && (!entry->custom_ms || sig->timeframe_ms < entry->custom_ms))
                entry->custom_ms = sig->timeframe_ms;
        }
        entry->next_due_ms = entry->first_start_ms;
    }
}
/* =============================================================================
 * TX FRAME LOG - one machine-readable line per frame handed to the CAN driver:
 *   $TX,<seq>,<t_ms>,<can_id hex8>,<data hex16>,<Q|B|E>
 * seq counts every send attempt, so a gap in seq means the UART log dropped
 * lines (TX buffer full), not that the frame was lost on the bus. The line is
 * skipped instead of blocking when the UART cannot keep up.
 * ============================================================================= */
static bool     s_tx_log_enabled = false;
static uint32_t s_tx_seq = 0u;
static uint32_t s_tx_log_dropped = 0u;

static char* prv_put_hex(char* p, uint32_t value, uint8_t digits)
{
    static const char k_hex[] = "0123456789ABCDEF";
    for (int8_t d = (int8_t)digits - 1; d >= 0; d--) {
        *p++ = k_hex[(value >> ((uint8_t)d * 4u)) & 0xFu];
    }
    return p;
}

static char* prv_put_u32(char* p, uint32_t value)
{
    char tmp[10];
    uint8_t n = 0u;
    do {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value > 0u);
    while (n > 0u) {
        *p++ = tmp[--n];
    }
    return p;
}

static void prv_log_tx_frame(const Sched_PGN_Entry_t* entry,
                             const uint8_t* payload,
                             J1939_Link_Status_t status,
                             uint32_t seq,
                             uint32_t now_ms)
{
    J1939_Frame_Header_t hdr;
    hdr.pgn = entry->timing.pgn;
    hdr.priority = entry->timing.priority;
    hdr.source_address = entry->timing.source_address;
    hdr.destination_address = J1939_ADDR_GLOBAL;

    char line[64];
    char* p = line;
    *p++ = '$'; *p++ = 'T'; *p++ = 'X'; *p++ = ',';
    p = prv_put_u32(p, seq);
    *p++ = ',';
    p = prv_put_u32(p, now_ms);
    *p++ = ',';
    p = prv_put_hex(p, J1939_Frame_Build_Id(&hdr), 8u);
    *p++ = ',';
    for (uint8_t i = 0u; i < 8u; i++) {
        p = prv_put_hex(p, payload[i], 2u);
    }
    *p++ = ',';
    *p++ = (status == J1939_LINK_OK) ? 'Q' : ((status == J1939_LINK_BUSY) ? 'B' : 'E');
    *p = '\0';

    /* +2 for the CR LF added by write_line */
    if (hal_console_tx_free() < (uint16_t)(p - line) + 2u) {
        s_tx_log_dropped++;
        return;
    }
    hal_console_write_line(line);
}

void J1939_Sched_Set_Tx_Log(bool enable)
{
    s_tx_log_enabled = enable;
    s_tx_log_dropped = 0u;
}

bool J1939_Sched_Get_Tx_Log(void)
{
    return s_tx_log_enabled;
}

uint32_t J1939_Sched_Get_Tx_Log_Dropped(void)
{
    return s_tx_log_dropped;
}

uint32_t J1939_Sched_Get_Tx_Seq(void)
{
    return s_tx_seq;
}

static bool prv_active(const J1939_Sched_Signal_t* sig, uint64_t elapsed)
{
    return elapsed >= sig->start_ms &&
           (sig->duration_ms == 0u || elapsed - sig->start_ms < sig->duration_ms);
}

void J1939_Sched_Get_Stats(J1939_Sched_Stats_t* stats)
{
    if (stats) *stats = s_stats;
}

bool J1939_Sched_Is_Complete(uint32_t now_ms)
{
    uint64_t elapsed = s_elapsed_ms + (uint32_t)(now_ms - s_last_time_ms);
    for (uint8_t i = 0; i < s_signal_count; i++) {
        const J1939_Sched_Signal_t* sig = &s_signals[i];
        if (sig->duration_ms == 0u || elapsed < (uint64_t)sig->start_ms + sig->duration_ms)
            return false;
    }
    return true;
}

/* Count the union of active slot ranges: shared fields must not double-count
 * a missed PGN, and inactive gaps must not look like scheduler failures. */
static uint64_t prv_missed_slots(const Sched_PGN_Entry_t* entry, uint64_t first,
                                 uint64_t count, uint32_t period)
{
    struct { uint64_t lo, hi; } ranges[J1939_SCHED_MAX_SIGNALS];
    unsigned n = 0u;
    for (unsigned i = 0; i < s_signal_count; i++) {
        const J1939_Sched_Signal_t* sig = &s_signals[i];
        if (sig->pgn != entry->timing.pgn) continue;
        uint64_t lo = sig->start_ms <= first ? 0u :
                      ((uint64_t)sig->start_ms - first + period - 1u) / period;
        uint64_t hi = count;
        if (sig->duration_ms) {
            uint64_t end = (uint64_t)sig->start_ms + sig->duration_ms;
            hi = end <= first ? 0u : (end - first + period - 1u) / period;
            if (hi > count) hi = count;
        }
        if (lo >= hi) continue;
        unsigned j = n++;
        while (j && ranges[j - 1].lo > lo) { ranges[j] = ranges[j - 1]; --j; }
        ranges[j].lo = lo;
        ranges[j].hi = hi;
    }
    uint64_t covered = 0u, end = 0u;
    for (unsigned i = 0; i < n; i++) {
        uint64_t lo = ranges[i].lo > end ? ranges[i].lo : end;
        if (ranges[i].hi > lo) covered += ranges[i].hi - lo;
        if (ranges[i].hi > end) end = ranges[i].hi;
    }
    return covered;
}

void J1939_Sched_Tick(uint32_t now_ms, J1939_Sched_Tick_Result_t* result)
{
    J1939_Sched_Tick_Result_t local;
    memset(&local, 0, sizeof(local));
    s_elapsed_ms += (uint32_t)(now_ms - s_last_time_ms);
    s_last_time_ms = now_ms;

    for (uint8_t i = 0; i < s_pgn_count; i++) {
        Sched_PGN_Entry_t* entry = &s_pgns[i];

        if (s_elapsed_ms < entry->next_due_ms) continue;
        uint32_t period = prv_period_ms(entry);
        uint64_t skipped = (s_elapsed_ms - entry->next_due_ms) / period;
        uint64_t missed = prv_missed_slots(entry, entry->next_due_ms, skipped, period);
        uint64_t total = (uint64_t)s_stats.missed_deadlines + missed;
        s_stats.missed_deadlines = total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
        /* Keep phase anchored to run start, sending at most once per PGN/tick. */
        entry->next_due_ms += (skipped + 1u) * period;

        /* Unassigned bytes stay 0xFF ("not available") */
        uint8_t payload[8];
        J1939_Initialize_Payload(payload);

        const J1939_Signal_Definition_t* watch_def = NULL;
        float watch_value = 0.0f;
        bool any_active = false;

        for (uint8_t s = 0; s < s_signal_count; s++) {
            const J1939_Sched_Signal_t* sig = &s_signals[s];
            if (sig->pgn == entry->timing.pgn && prv_active(sig, s_elapsed_ms)) {
                any_active = true;
                float value = Pattern_Generator_Get_Value_Instant(sig->spn, now_ms, sig->def->min_physical);
                J1939_Encode_Signal(sig->def, value, payload);
                if (sig->spn == g_j1939_dbg.watch_spn) {
                    watch_def = sig->def;
                    watch_value = value;
                }
            }
        }

        if (!any_active) continue;

        J1939_Link_Status_t status = J1939_Link_Send(entry->timing.pgn, payload, 8u,
                                                     entry->timing.priority, entry->timing.source_address);
        s_tx_seq++;
        if (s_tx_log_enabled) {
            prv_log_tx_frame(entry, payload, status, s_tx_seq, now_ms);
        }

        switch (status) {
            case J1939_LINK_OK:   local.sent++;   g_j1939_dbg.tx_ok++;    break;
            case J1939_LINK_BUSY: local.busy++;   g_j1939_dbg.tx_busy++;  break;
            default:              local.failed++; g_j1939_dbg.tx_error++; break;
        }

        if (watch_def != NULL) {
            prv_debug_capture(entry, watch_def, watch_value, payload, status, now_ms);
        }
    }

    s_stats.queued += local.sent;
    s_stats.busy += local.busy;
    s_stats.failed += local.failed;
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
