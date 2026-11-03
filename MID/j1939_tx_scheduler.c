/*
 * =============================================================================
 * FILE : j1939_tx_scheduler.c
 * WHAT : PGN-centric deadline scheduler backed directly by generated catalog.
 * =============================================================================
 */

#include "j1939_tx_scheduler.h"
#include "j1939_encode_decode.h"
#include "j1939_link.h"
#include "j1939_tp.h"
#include "j1939_value_store.h"
#include "hal_console.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t next_due_ms;
    uint8_t catalog_pgn_index;
    uint8_t tp_pending;     /* TP PGN came due while another BAM was running */
    uint8_t reserved[2];
} Sched_PGN_Entry_t;

typedef struct {
    uint32_t deadline_ms;
    uint16_t period_ms;
    uint16_t watch_catalog_index;
    uint8_t payload[8];
    uint8_t pgn_slot;
} Sched_Tx_Entry_t;

_Static_assert(sizeof(Sched_PGN_Entry_t) == 8u,
               "PGN runtime entry must remain compact");
_Static_assert(sizeof(Sched_Tx_Entry_t) == 20u,
               "TX queue entry must remain compact");

static Sched_PGN_Entry_t   s_pgns[J1939_SCHED_MAX_PGNS];
static uint8_t             s_pgn_count;
static Sched_Tx_Entry_t    s_tx_queue[J1939_TX_QUEUE_CAPACITY];
static uint8_t             s_tx_queue_count;
static uint32_t            s_tx_retry_not_before_ms;
static J1939_Sched_Stats_t s_stats;
static J1939_Sched_Mode_t  s_mode = J1939_SCHED_MODE_SMOOTH;

volatile J1939_Debug_Watch_t g_j1939_dbg = {
    .watch_catalog_index = J1939_CATALOG_INDEX_INVALID
};

static bool     s_tx_log_enabled;
static uint32_t s_tx_seq;
static uint32_t s_tx_log_dropped;

static bool prv_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static const J1939_Catalog_PGN_t* prv_catalog_pgn(
    const Sched_PGN_Entry_t* entry)
{
    return &J1939_Catalog_PGNs[entry->catalog_pgn_index];
}

static uint16_t prv_period_ms(const Sched_PGN_Entry_t* entry)
{
    /* Generated, reviewed periods are authoritative in every legacy run mode. */
    return prv_catalog_pgn(entry)->period_ms;
}

/* Multi-packet PGNs are broadcast with BAM (j1939_tp.c). */
static bool prv_pgn_needs_tp(const J1939_Catalog_PGN_t* pgn)
{
    return (pgn->payload_length > 8u) ||
           ((pgn->flags & J1939_CAT_PGN_REQUIRES_TP) != 0u);
}

/* period_ms is 0 only for a PGN that is sent solely in answer to a request
 * (see J1939_Catalog_PGN_t); an approved ON_REQUEST demo broadcast has one. */
static bool prv_pgn_is_schedulable(const J1939_Catalog_PGN_t* pgn)
{
    return (pgn->period_ms > 0u) &&
           (pgn->payload_length <= J1939_TP_MAX_PAYLOAD_BYTES);
}

static bool prv_signal_is_active(uint16_t catalog_index)
{
    return J1939_Value_Store_Is_Active(catalog_index) ||
           Pattern_Generator_Is_Registered(catalog_index);
}

static Sched_PGN_Entry_t* prv_find_pgn(uint8_t catalog_pgn_index)
{
    for (uint8_t i = 0u; i < s_pgn_count; i++) {
        if (s_pgns[i].catalog_pgn_index == catalog_pgn_index) {
            return &s_pgns[i];
        }
    }
    return NULL;
}

static Sched_PGN_Entry_t* prv_add_pgn(uint8_t catalog_pgn_index,
                                      uint32_t now_ms)
{
    Sched_PGN_Entry_t* entry = prv_find_pgn(catalog_pgn_index);
    if (entry != NULL) {
        return entry;
    }
    if (s_pgn_count >= J1939_SCHED_MAX_PGNS) {
        return NULL;
    }

    entry = &s_pgns[s_pgn_count++];
    memset(entry, 0, sizeof(*entry));
    entry->catalog_pgn_index = catalog_pgn_index;
    entry->next_due_ms = now_ms + prv_period_ms(entry);
    return entry;
}

static uint32_t prv_can_id(const Sched_PGN_Entry_t* entry)
{
    const J1939_Catalog_PGN_t* pgn = prv_catalog_pgn(entry);
    J1939_Frame_Header_t hdr;
    hdr.pgn = pgn->pgn;
    hdr.priority = pgn->priority;
    hdr.source_address = pgn->source_address;
    hdr.destination_address = J1939_ADDR_GLOBAL;
    return J1939_Frame_Build_Id(&hdr);
}

static uint8_t prv_queue_class(uint16_t period_ms)
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
    return prv_can_id(&s_pgns[lhs->pgn_slot]) <
           prv_can_id(&s_pgns[rhs->pgn_slot]);
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
    p = prv_put_hex(p, prv_can_id(&s_pgns[frame->pgn_slot]), 8u);
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
    if (frame->watch_catalog_index == J1939_CATALOG_INDEX_INVALID) {
        return;
    }

    const J1939_Catalog_Signal_t* sig =
        &J1939_Catalog_Signals[frame->watch_catalog_index];
    const J1939_Catalog_Scale_t* scale =
        &J1939_Catalog_Scales[sig->scale_index];
    uint32_t raw = 0u;
    if (!J1939_Unpack_Raw(frame->payload, sizeof(frame->payload),
                          sig->start_bit, (uint8_t)sig->bit_length, &raw)) {
        return;
    }

    g_j1939_dbg.decoded = ((float)raw * scale->resolution) + scale->offset;
    g_j1939_dbg.can_id = prv_can_id(&s_pgns[frame->pgn_slot]);
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
        /* A frame one full period past its deadline is superseded by the next
         * instance of its PGN. Sending it after a no-ACK/bus-off outage would
         * only replay a burst of stale data once the bus accepts frames. */
        if ((frame->period_ms != 0u) &&
            prv_time_reached(now_ms, frame->deadline_ms + frame->period_ms)) {
            result->expired++;
            s_stats.expired++;
            prv_pop_queue_front();
            continue;
        }
        const J1939_Catalog_PGN_t* pgn =
            prv_catalog_pgn(&s_pgns[frame->pgn_slot]);
        const J1939_Link_Status_t status =
            J1939_Link_Send(pgn->pgn, frame->payload, sizeof(frame->payload),
                            pgn->priority, pgn->source_address);

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
        s_tx_retry_not_before_ms = now_ms + 1u;
        break;
    }
}

static uint32_t prv_raw_from_physical(const J1939_Catalog_Signal_t* sig,
                                      float physical)
{
    const J1939_Catalog_Scale_t* scale =
        &J1939_Catalog_Scales[sig->scale_index];
    const float raw_f = (physical - scale->offset) / scale->resolution;
    if (raw_f <= (float)sig->raw_min) {
        return sig->raw_min;
    }
    if (raw_f >= (float)sig->raw_max) {
        return sig->raw_max;
    }
    return (uint32_t)(raw_f + 0.5f);
}

/* ASCII blob (VIN): text, then the J1939 terminator, rest stays 0xFF. Text
 * that does not fit leaves room for the terminator. */
static bool prv_pack_text(uint8_t* payload,
                          uint16_t length,
                          const J1939_Catalog_Signal_t* sig,
                          const uint8_t* text,
                          uint16_t text_length)
{
    const uint16_t offset = (uint16_t)(sig->start_bit / 8u);
    if (offset >= length) {
        return false;
    }
    uint16_t room = (uint16_t)(length - offset);
    if (J1939_VIN_TERMINATOR != 0) {
        room--;
    }
    const uint16_t count = (text_length < room) ? text_length : room;
    memcpy(&payload[offset], text, count);
    if (J1939_VIN_TERMINATOR != 0) {
        payload[offset + count] = (uint8_t)J1939_VIN_TERMINATOR;
    }
    return true;
}

/* Bytes a PGN occupies on the bus: the variable-length VIN PGN uses the fixed
 * demo frame J1939_VIN_TX_BYTES instead of its 200-byte catalog maximum. */
static uint16_t prv_tx_length(const J1939_Catalog_PGN_t* pgn)
{
    return ((pgn->flags & J1939_CAT_PGN_VARIABLE_LENGTH) != 0u)
        ? (uint16_t)J1939_VIN_TX_BYTES : pgn->payload_length;
}

/* Pack every active signal of one PGN into payload[0..length); false when no
 * signal of the PGN is active. watch_index (8-byte frames only) receives the
 * debug-watched catalog index when it is packed. */
static bool prv_pack_pgn(uint8_t pgn_slot,
                         uint32_t now_ms,
                         uint8_t* payload,
                         uint16_t length,
                         uint16_t* watch_index)
{
    const J1939_Catalog_PGN_t* pgn = prv_catalog_pgn(&s_pgns[pgn_slot]);
    bool packed_any = false;
    const uint16_t end = (uint16_t)(pgn->first_signal + pgn->signal_count);
    for (uint16_t i = pgn->first_signal; i < end; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        uint32_t raw = 0u;
        float physical = 0.0f;

        const uint8_t* blob = NULL;
        uint16_t blob_length = 0u;
        if (J1939_Value_Store_Get_Raw(i, &raw)) {
            const J1939_Catalog_Scale_t* scale =
                &J1939_Catalog_Scales[sig->scale_index];
            physical = ((float)raw * scale->resolution) + scale->offset;
        } else if (J1939_Value_Store_Get_Blob(i, &blob, &blob_length)) {
            packed_any = prv_pack_text(payload, length, sig, blob, blob_length) ||
                         packed_any;
            continue;
        } else if (Pattern_Generator_Is_Registered(i)) {
            physical = Pattern_Generator_Get_Value_Instant(i, now_ms, 0.0f);
            raw = prv_raw_from_physical(sig, physical);
        } else {
            continue;
        }

        if (J1939_Pack_Raw(payload, length,
                           sig->start_bit, (uint8_t)sig->bit_length, raw)) {
            packed_any = true;
            if ((watch_index != NULL) && (i == g_j1939_dbg.watch_catalog_index)) {
                *watch_index = i;
                g_j1939_dbg.value = physical;
            }
        }
    }
    return packed_any;
}

static void prv_schedule_due_pgn(uint8_t pgn_slot,
                                 uint16_t period_ms,
                                 uint32_t deadline_ms,
                                 uint32_t now_ms,
                                 J1939_Sched_Tick_Result_t* result)
{
    Sched_Tx_Entry_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.pgn_slot = pgn_slot;
    frame.deadline_ms = deadline_ms;
    frame.period_ms = period_ms;
    frame.watch_catalog_index = J1939_CATALOG_INDEX_INVALID;
    J1939_Initialize_Payload(frame.payload);

    if (!prv_pack_pgn(pgn_slot, now_ms, frame.payload, sizeof(frame.payload),
                      &frame.watch_catalog_index)) {
        return;
    }
    if (prv_enqueue(&frame)) {
        result->enqueued++;
    } else {
        result->dropped++;
    }
}

/* Snapshot a multi-packet PGN into the BAM session, or mark it pending when
 * another BAM is still running (one TP session at a time). */
static void prv_start_tp_pgn(uint8_t pgn_slot, uint32_t now_ms)
{
    Sched_PGN_Entry_t* entry = &s_pgns[pgn_slot];
    const J1939_Catalog_PGN_t* pgn = prv_catalog_pgn(entry);
    const uint16_t length = prv_tx_length(pgn);
    uint8_t* payload = J1939_Tp_Bam_Prepare(length);
    if (payload == NULL) {
        entry->tp_pending = 1u;
        return;
    }
    entry->tp_pending = 0u;
    if (prv_pack_pgn(pgn_slot, now_ms, payload, length, NULL)) {
        (void)J1939_Tp_Bam_Start(pgn->pgn, pgn->source_address, now_ms);
    }
}

bool J1939_Sched_Pgn_Is_Schedulable(uint8_t catalog_pgn_index)
{
    return (catalog_pgn_index < J1939_CATALOG_PGN_COUNT) &&
           prv_pgn_is_schedulable(&J1939_Catalog_PGNs[catalog_pgn_index]);
}

void J1939_Sched_Clear(void)
{
    s_pgn_count = 0u;
    s_tx_queue_count = 0u;
    s_tx_retry_not_before_ms = 0u;
    memset(&s_stats, 0, sizeof(s_stats));
    J1939_Tp_Init();

    if (g_j1939_dbg.watch_catalog_index == J1939_CATALOG_INDEX_INVALID) {
        g_j1939_dbg.watch_catalog_index =
            J1939_Catalog_Find_Signal_Index(
                J1939_Catalog_PGNs[J1939_CAT_PGN_IDX_EEC1].pgn, 190u);
    }
}

bool J1939_Sched_Can_Add_Catalog_Signal(uint16_t catalog_index)
{
    if (catalog_index >= J1939_CATALOG_SIGNAL_COUNT) {
        return false;
    }
    if (!prv_signal_is_active(catalog_index) &&
        (J1939_Sched_Get_Signal_Count() >= J1939_PROFILE_SIGNAL_CAPACITY)) {
        return false;
    }

    const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[catalog_index];
    const J1939_Catalog_PGN_t* pgn = &J1939_Catalog_PGNs[sig->pgn_index];
    if (!prv_pgn_is_schedulable(pgn)) {
        return true;
    }
    return (prv_find_pgn(sig->pgn_index) != NULL) ||
           (s_pgn_count < J1939_SCHED_MAX_PGNS);
}

bool J1939_Sched_Add_Catalog_Signal(uint16_t catalog_index,
                                    uint32_t timeframe_ms,
                                    uint32_t now_ms)
{
    (void)timeframe_ms;
    if (!J1939_Sched_Can_Add_Catalog_Signal(catalog_index)) {
        return false;
    }

    const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[catalog_index];
    const J1939_Catalog_PGN_t* pgn = &J1939_Catalog_PGNs[sig->pgn_index];
    if (!prv_pgn_is_schedulable(pgn)) {
        /* Request-only records are configured but never self-transmit. */
        return true;
    }
    return prv_add_pgn(sig->pgn_index, now_ms) != NULL;
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
    J1939_Tp_Cancel();

    for (uint8_t i = 0u; i < s_pgn_count; i++) {
        const uint16_t period_ms = prv_period_ms(&s_pgns[i]);
        uint8_t rank = 0u;
        uint8_t peers = 0u;
        for (uint8_t j = 0u; j < s_pgn_count; j++) {
            if (prv_period_ms(&s_pgns[j]) == period_ms) {
                if (j < i) {
                    rank++;
                }
                peers++;
            }
        }
        const uint32_t phase =
            (((uint32_t)(rank + 1u) * period_ms) + peers - 1u) / peers;
        s_pgns[i].next_due_ms = now_ms + phase;
        s_pgns[i].tp_pending = 0u;
    }
}

void J1939_Sched_Tick(uint32_t now_ms, J1939_Sched_Tick_Result_t* result)
{
    J1939_Sched_Tick_Result_t local;
    memset(&local, 0, sizeof(local));

    for (uint8_t i = 0u; (i < s_pgn_count) && !J1939_Tp_Is_Busy(); i++) {
        if (s_pgns[i].tp_pending != 0u) {
            prv_start_tp_pgn(i, now_ms);
        }
    }

    for (uint8_t i = 0u; i < s_pgn_count; i++) {
        Sched_PGN_Entry_t* entry = &s_pgns[i];
        const uint16_t period_ms = prv_period_ms(entry);
        if ((period_ms == 0u) ||
            !prv_time_reached(now_ms, entry->next_due_ms)) {
            continue;
        }

        const uint32_t periods_late =
            (now_ms - entry->next_due_ms) / period_ms;
        /* The frame stands for the latest due slot; skipped slots are only
         * counted, so the expiry check in the drain measures real age. */
        const uint32_t deadline_ms =
            entry->next_due_ms + (periods_late * period_ms);
        if (periods_late > 0u) {
            local.deadline_missed += (uint16_t)periods_late;
            s_stats.deadline_missed += periods_late;
        }
        entry->next_due_ms += (periods_late + 1u) * period_ms;
        if (prv_pgn_needs_tp(prv_catalog_pgn(entry))) {
            prv_start_tp_pgn(i, now_ms);
        } else {
            prv_schedule_due_pgn(i, period_ms, deadline_ms, now_ms, &local);
        }
    }

    prv_drain_queue(now_ms, &local);
    /* After the short frames: BAM packets tolerate 50..200 ms spacing. */
    J1939_Tp_Process(now_ms);
    if (result != NULL) {
        *result = local;
    }
}

uint16_t J1939_Sched_Get_Signal_Count(void)
{
    uint16_t count = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        if (prv_signal_is_active(i)) {
            count++;
        }
    }
    return count;
}

bool J1939_Sched_Get_Active_Catalog_Index(uint16_t ordinal,
                                          uint16_t* out_catalog_index)
{
    if (out_catalog_index == NULL) {
        return false;
    }
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        if (prv_signal_is_active(i)) {
            if (ordinal == 0u) {
                *out_catalog_index = i;
                return true;
            }
            ordinal--;
        }
    }
    return false;
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
