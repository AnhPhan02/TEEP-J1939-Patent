/*
 * =============================================================================
 * FILE : j1939_tx_scheduler.c
 * WHAT : PGN-centric deadline scheduler with a software TX queue.
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
    uint32_t custom_ms;
    uint32_t next_due_ms;
} Sched_PGN_Entry_t;

typedef struct {
    Sched_PGN_Entry_t* pgn_entry;
    const J1939_Signal_Definition_t* watch_def;
    float watch_value;
    uint32_t deadline_ms;
    uint32_t period_ms;
    uint8_t payload[8];
} Sched_Tx_Entry_t;

static J1939_Sched_Signal_t s_signals[J1939_SCHED_MAX_SIGNALS];
static uint8_t              s_signal_count;
static Sched_PGN_Entry_t    s_pgns[J1939_SCHED_MAX_PGNS];
static uint8_t              s_pgn_count;
static Sched_Tx_Entry_t     s_tx_queue[J1939_TX_QUEUE_CAPACITY];
static uint8_t              s_tx_queue_count;
static uint32_t             s_tx_retry_not_before_ms;
static J1939_Sched_Stats_t  s_stats;
static J1939_Sched_Mode_t   s_mode = J1939_SCHED_MODE_SMOOTH;

volatile J1939_Debug_Watch_t g_j1939_dbg = {
    .watch_signal_id = J1939_SIGNAL_ID_INVALID
};

static bool     s_tx_log_enabled;
static uint32_t s_tx_seq;
static uint32_t s_tx_log_dropped;

static uint32_t prv_can_id(const Sched_PGN_Entry_t* entry);

static Sched_PGN_Entry_t* prv_find_pgn(uint32_t pgn)
{
    for (uint8_t i = 0u; i < s_pgn_count; i++) {
        if (s_pgns[i].timing.pgn == pgn) {
            return &s_pgns[i];
        }
    }
    return NULL;
}

static J1939_Sched_Signal_t* prv_find_signal(J1939_Signal_Id_t signal_id)
{
    for (uint8_t i = 0u; i < s_signal_count; i++) {
        if (s_signals[i].signal_id == signal_id) {
            return &s_signals[i];
        }
    }
    return NULL;
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

static Sched_PGN_Entry_t* prv_add_pgn(uint32_t pgn, uint32_t now_ms)
{
    Sched_PGN_Entry_t* entry = prv_find_pgn(pgn);
    if (entry != NULL) {
        return entry;
    }
    if (s_pgn_count >= J1939_SCHED_MAX_PGNS) {
        return NULL;
    }

    entry = &s_pgns[s_pgn_count++];
    J1939_PGN_Get_Timing(pgn, &entry->timing);
    entry->custom_ms = 0u;
    entry->next_due_ms = now_ms + prv_period_ms(entry);
    return entry;
}

static bool prv_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static uint8_t prv_queue_class(uint32_t period_ms)
{
    return (period_ms <= 20u) ? 0u : 1u;
}

static bool prv_queue_before(const Sched_Tx_Entry_t* lhs,
                             const Sched_Tx_Entry_t* rhs)
{
    const uint8_t lhs_class = prv_queue_class(lhs->period_ms);
    const uint8_t rhs_class = prv_queue_class(rhs->period_ms);
    if (lhs_class != rhs_class) {
        return lhs_class < rhs_class;
    }
    if (lhs->deadline_ms != rhs->deadline_ms) {
        return (int32_t)(lhs->deadline_ms - rhs->deadline_ms) < 0;
    }
    return prv_can_id(lhs->pgn_entry) < prv_can_id(rhs->pgn_entry);
}

static bool prv_enqueue(const Sched_Tx_Entry_t* frame)
{
    if (s_tx_queue_count >= J1939_TX_QUEUE_CAPACITY) {
        s_stats.dropped++;
        return false;
    }

    uint8_t pos = s_tx_queue_count;
    while ((pos > 0u) && prv_queue_before(frame, &s_tx_queue[pos - 1u])) {
        s_tx_queue[pos] = s_tx_queue[pos - 1u];
        pos--;
    }
    s_tx_queue[pos] = *frame;
    s_tx_queue_count++;
    s_stats.enqueued++;
    s_stats.queued = s_tx_queue_count;
    if (s_tx_queue_count > s_stats.high_water) {
        s_stats.high_water = s_tx_queue_count;
    }
    return true;
}

static void prv_pop_queue_front(void)
{
    for (uint8_t i = 1u; i < s_tx_queue_count; i++) {
        s_tx_queue[i - 1u] = s_tx_queue[i];
    }
    if (s_tx_queue_count > 0u) {
        s_tx_queue_count--;
    }
    s_stats.queued = s_tx_queue_count;
}

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

static uint32_t prv_can_id(const Sched_PGN_Entry_t* entry)
{
    J1939_Frame_Header_t hdr;
    hdr.pgn = entry->timing.pgn;
    hdr.priority = entry->timing.priority;
    hdr.source_address = entry->timing.source_address;
    hdr.destination_address = J1939_ADDR_GLOBAL;
    return J1939_Frame_Build_Id(&hdr);
}

static void prv_log_tx_frame(const Sched_Tx_Entry_t* frame,
                             J1939_Link_Status_t status,
                             uint32_t seq,
                             uint32_t now_ms)
{
    char line[64];
    char* p = line;
    *p++ = '$'; *p++ = 'T'; *p++ = 'X'; *p++ = ',';
    p = prv_put_u32(p, seq);
    *p++ = ',';
    p = prv_put_u32(p, now_ms);
    *p++ = ',';
    p = prv_put_hex(p, prv_can_id(frame->pgn_entry), 8u);
    *p++ = ',';
    for (uint8_t i = 0u; i < 8u; i++) {
        p = prv_put_hex(p, frame->payload[i], 2u);
    }
    *p++ = ',';
    *p++ = (status == J1939_LINK_OK) ? 'Q' :
           ((status == J1939_LINK_BUSY) ? 'B' : 'E');
    *p = '\0';

    if (hal_console_tx_free() < (uint16_t)(p - line) + 2u) {
        s_tx_log_dropped++;
        return;
    }
    hal_console_write_line(line);
}

static void prv_debug_capture(const Sched_Tx_Entry_t* frame,
                              J1939_Link_Status_t status,
                              uint32_t now_ms)
{
    if (frame->watch_def == NULL) {
        return;
    }

    float decoded = 0.0f;
    (void)J1939_Decode_Signal_Sized(frame->watch_def, frame->payload,
                                    sizeof(frame->payload), &decoded);
    g_j1939_dbg.value = frame->watch_value;
    g_j1939_dbg.decoded = decoded;
    g_j1939_dbg.can_id = prv_can_id(frame->pgn_entry);
    for (uint8_t i = 0u; i < 8u; i++) {
        g_j1939_dbg.data[i] = frame->payload[i];
    }
    g_j1939_dbg.link_status = (uint8_t)status;
    g_j1939_dbg.timestamp_ms = now_ms;
}

static void prv_drain_queue(uint32_t now_ms,
                            J1939_Sched_Tick_Result_t* result)
{
    if ((s_tx_queue_count > 0u) &&
        !prv_time_reached(now_ms, s_tx_retry_not_before_ms)) {
        return;
    }

    while (s_tx_queue_count > 0u) {
        Sched_Tx_Entry_t* frame = &s_tx_queue[0];
        const J1939_PGN_Timing_t* timing = &frame->pgn_entry->timing;
        const J1939_Link_Status_t status =
            J1939_Link_Send(timing->pgn, frame->payload,
                            sizeof(frame->payload), timing->priority,
                            timing->source_address);

        s_tx_seq++;
        if (s_tx_log_enabled) {
            prv_log_tx_frame(frame, status, s_tx_seq, now_ms);
        }
        prv_debug_capture(frame, status, now_ms);

        if (status == J1939_LINK_OK) {
            result->sent++;
            s_stats.sent++;
            g_j1939_dbg.tx_ok++;
            prv_pop_queue_front();
            continue;
        }
        if (status == J1939_LINK_BUSY) {
            result->busy++;
            s_stats.busy++;
            g_j1939_dbg.tx_busy++;
        } else {
            result->failed++;
            s_stats.failed++;
            g_j1939_dbg.tx_error++;
        }
        /* Retain the frame for a later main-loop pass. */
        s_tx_retry_not_before_ms = now_ms + 1u;
        break;
    }
}

static void prv_schedule_due_pgn(Sched_PGN_Entry_t* entry,
                                 uint32_t period_ms,
                                 uint32_t deadline_ms,
                                 uint32_t now_ms,
                                 J1939_Sched_Tick_Result_t* result)
{
    Sched_Tx_Entry_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.pgn_entry = entry;
    frame.deadline_ms = deadline_ms;
    frame.period_ms = period_ms;
    J1939_Initialize_Payload(frame.payload);

    for (uint8_t s = 0u; s < s_signal_count; s++) {
        const J1939_Sched_Signal_t* sig = &s_signals[s];
        if (sig->def->pgn != entry->timing.pgn) {
            continue;
        }
        const float value = Pattern_Generator_Get_Value_Instant(
            sig->signal_id, now_ms, sig->def->min_physical);
        (void)J1939_Encode_Signal_Sized(sig->def, value, frame.payload,
                                        sizeof(frame.payload));
        if (sig->signal_id == g_j1939_dbg.watch_signal_id) {
            frame.watch_def = sig->def;
            frame.watch_value = value;
        }
    }

    if (prv_enqueue(&frame)) {
        result->enqueued++;
    } else {
        result->dropped++;
    }
}

void J1939_Sched_Clear(void)
{
    s_signal_count = 0u;
    s_pgn_count = 0u;
    s_tx_queue_count = 0u;
    s_tx_retry_not_before_ms = 0u;
    memset(&s_stats, 0, sizeof(s_stats));

    if (g_j1939_dbg.watch_signal_id == J1939_SIGNAL_ID_INVALID) {
        const J1939_Signal_Definition_t* engine_speed =
            J1939_Find_Signal_By_PGN_SPN(PGN_EEC1, SPN_ENGINE_SPEED);
        J1939_Signal_Id_t signal_id = J1939_SIGNAL_ID_INVALID;
        if (J1939_Get_Signal_Id(engine_speed, &signal_id)) {
            g_j1939_dbg.watch_signal_id = signal_id;
        }
    }
}

bool J1939_Sched_Can_Add_Signal(const J1939_Signal_Definition_t* def)
{
    J1939_Signal_Id_t signal_id = J1939_SIGNAL_ID_INVALID;
    if (!J1939_Get_Signal_Id(def, &signal_id)) {
        return false;
    }
    if (prv_find_signal(signal_id) != NULL) {
        return true;
    }
    if (s_signal_count >= J1939_SCHED_MAX_SIGNALS) {
        return false;
    }
    return (prv_find_pgn(def->pgn) != NULL) ||
           (s_pgn_count < J1939_SCHED_MAX_PGNS);
}

bool J1939_Sched_Add_Signal(const J1939_Signal_Definition_t* def,
                            uint32_t timeframe_ms,
                            float t_start,
                            float t_dur,
                            uint32_t now_ms)
{
    J1939_Signal_Id_t signal_id = J1939_SIGNAL_ID_INVALID;
    if (!J1939_Get_Signal_Id(def, &signal_id) ||
        !J1939_Sched_Can_Add_Signal(def)) {
        return false;
    }

    Sched_PGN_Entry_t* entry = prv_add_pgn(def->pgn, now_ms);
    if (entry == NULL) {
        return false;
    }

    J1939_Sched_Signal_t* sig = prv_find_signal(signal_id);
    if (sig == NULL) {
        sig = &s_signals[s_signal_count++];
    }
    sig->signal_id = signal_id;
    sig->def = def;
    sig->timeframe_ms = timeframe_ms;
    sig->t_start = t_start;
    sig->t_dur = t_dur;

    if ((timeframe_ms > 0u) &&
        ((entry->custom_ms == 0u) || (timeframe_ms < entry->custom_ms))) {
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
    s_tx_queue_count = 0u;
    s_tx_retry_not_before_ms = now_ms;
    s_stats.queued = 0u;
    for (uint8_t i = 0u; i < s_pgn_count; i++) {
        const uint32_t period_ms = prv_period_ms(&s_pgns[i]);
        s_pgns[i].next_due_ms = now_ms + period_ms;
    }
}

void J1939_Sched_Tick(uint32_t now_ms, J1939_Sched_Tick_Result_t* result)
{
    J1939_Sched_Tick_Result_t local;
    memset(&local, 0, sizeof(local));

    for (uint8_t i = 0u; i < s_pgn_count; i++) {
        Sched_PGN_Entry_t* entry = &s_pgns[i];
        const uint32_t period_ms = prv_period_ms(entry);
        if ((period_ms == 0u) ||
            !prv_time_reached(now_ms, entry->next_due_ms)) {
            continue;
        }

        const uint32_t deadline_ms = entry->next_due_ms;
        const uint32_t periods_late =
            (now_ms - entry->next_due_ms) / period_ms;
        if (periods_late > 0u) {
            local.deadline_missed += (uint16_t)periods_late;
            s_stats.deadline_missed += periods_late;
        }
        entry->next_due_ms += (periods_late + 1u) * period_ms;
        prv_schedule_due_pgn(entry, period_ms, deadline_ms, now_ms, &local);
    }

    prv_drain_queue(now_ms, &local);
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

void J1939_Sched_Get_Stats(J1939_Sched_Stats_t* stats)
{
    if (stats != NULL) {
        *stats = s_stats;
        stats->queued = s_tx_queue_count;
    }
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
