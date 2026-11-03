#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal_console.h"
#include "j1939_encode_decode.h"
#include "j1939_link.h"
#include "j1939_pattern_generator.h"
#include "j1939_tx_scheduler.h"
#include "j1939_value_store.h"

#define EXPECTED_FIXED_PGNS       45u
#define EXPECTED_FIXED_FPS       697u
#define EXPECTED_RUNTIME_PGNS     60u   /* 57 single-frame + FEE1/FEE3/VI BAM */
#define EXPECTED_ALL_MODE_FPS    717u   /* 697 + EBS21 10 + 8 demo + OELC/LC 2 */

static uint16_t s_sent_by_pgn[J1939_CATALOG_PGN_COUNT];
static uint8_t s_last_payload[J1939_CATALOG_PGN_COUNT][8];
static bool s_link_busy;

J1939_Link_Status_t J1939_Link_Send(uint32_t pgn,
                                    const uint8_t* data,
                                    uint8_t data_length,
                                    uint8_t priority,
                                    uint8_t source_address)
{
    (void)priority;
    (void)source_address;
    if (s_link_busy) {
        return J1939_LINK_BUSY;
    }
    assert(data_length == 8u);
    const uint16_t index = J1939_Catalog_Find_PGN_Index(pgn);
    assert(index != J1939_CATALOG_INDEX_INVALID);
    s_sent_by_pgn[index]++;
    memcpy(s_last_payload[index], data, 8u);
    return J1939_LINK_OK;
}

uint16_t hal_console_tx_free(void) { return UINT16_MAX; }
void hal_console_write_line(const char* str) { (void)str; }

static bool is_short_runtime_pgn(const J1939_Catalog_PGN_t* pgn)
{
    return (pgn->transmission_mode != J1939_CAT_TX_ON_REQUEST) &&
           (pgn->period_ms > 0u) &&
           (pgn->payload_length <= 8u) &&
           ((pgn->flags & J1939_CAT_PGN_REQUIRES_TP) == 0u);
}

static void configure_all_scalar_records(void)
{
    Pattern_Generator_Init(0u);
    J1939_Value_Store_Init();
    J1939_Sched_Clear();
    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_EMPTY) ==
           J1939_VALUE_STORE_OK);

    uint16_t scalar_count = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        if ((sig->flags & J1939_CAT_SIG_BLOB) == 0u) {
            assert(J1939_Value_Store_Set_Raw(sig->signal_key, sig->raw_min) ==
                   J1939_VALUE_STORE_OK);
            scalar_count++;
        }
    }
    assert(scalar_count == 178u);
    assert(J1939_Value_Store_Commit(NULL) == J1939_VALUE_STORE_OK);

    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        assert(J1939_Sched_Add_Catalog_Signal(i, 1u, 0u));
    }
    assert(J1939_Sched_Get_Signal_Count() == scalar_count);
    assert(J1939_Sched_Get_PGN_Count() == EXPECTED_RUNTIME_PGNS);
}

static void assert_last_payload_matches_store(uint8_t pgn_index)
{
    const J1939_Catalog_PGN_t* pgn = &J1939_Catalog_PGNs[pgn_index];
    const uint16_t end = (uint16_t)(pgn->first_signal + pgn->signal_count);
    for (uint16_t i = pgn->first_signal; i < end; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        if ((sig->flags & J1939_CAT_SIG_BLOB) != 0u) {
            continue;
        }
        uint32_t raw = UINT32_MAX;
        assert(J1939_Unpack_Raw(s_last_payload[pgn_index], 8u,
                                sig->start_bit, (uint8_t)sig->bit_length,
                                &raw));
        assert(raw == sig->raw_min);
    }
}

static void test_full_catalog_timing_and_payloads(void)
{
    memset(s_sent_by_pgn, 0, sizeof(s_sent_by_pgn));
    memset(s_last_payload, 0, sizeof(s_last_payload));
    s_link_busy = false;
    configure_all_scalar_records();
    J1939_Sched_Reset_Timers(0u);

    for (uint32_t now_ms = 1u; now_ms <= 1000u; now_ms++) {
        J1939_Sched_Tick(now_ms, NULL);
    }

    uint16_t fixed_pgn_count = 0u;
    uint32_t fixed_frames = 0u;
    uint32_t all_frames = 0u;
    for (uint8_t i = 0u; i < J1939_CATALOG_PGN_COUNT; i++) {
        const J1939_Catalog_PGN_t* pgn = &J1939_Catalog_PGNs[i];
        all_frames += s_sent_by_pgn[i];

        if (pgn->transmission_mode == J1939_CAT_TX_ON_REQUEST) {
            /* Approved demo broadcast at period_ms; VIN stays request-only. */
            assert(s_sent_by_pgn[i] ==
                   ((pgn->period_ms == 0u) ? 0u : (uint16_t)(1000u / pgn->period_ms)));
            if (s_sent_by_pgn[i] > 0u) {
                assert_last_payload_matches_store(i);
            }
            continue;
        }
        if ((pgn->transmission_mode == J1939_CAT_TX_PERIODIC) &&
            is_short_runtime_pgn(pgn)) {
            fixed_pgn_count++;
            fixed_frames += s_sent_by_pgn[i];
            assert(s_sent_by_pgn[i] == (uint16_t)(1000u / pgn->period_ms));
        }
        if ((pgn->transmission_mode == J1939_CAT_TX_ON_CHANGE) &&
            is_short_runtime_pgn(pgn)) {
            /* Event PGNs use only their explicit catalog fallback. */
            assert(pgn->period_ms == 1000u);
            assert(s_sent_by_pgn[i] == 1u);
        }
        if (s_sent_by_pgn[i] > 0u) {
            assert_last_payload_matches_store(i);
        }
    }
    assert(fixed_pgn_count == EXPECTED_FIXED_PGNS);
    assert(fixed_frames == EXPECTED_FIXED_FPS);
    assert(all_frames == EXPECTED_ALL_MODE_FPS);

    J1939_Sched_Stats_t stats;
    J1939_Sched_Get_Stats(&stats);
    assert(stats.enqueued == EXPECTED_ALL_MODE_FPS);
    assert(stats.sent == EXPECTED_ALL_MODE_FPS);
    assert(stats.dropped == 0u);
    assert(stats.deadline_missed == 0u);
    assert(stats.queued == 0u);
    assert(stats.high_water < J1939_TX_QUEUE_CAPACITY);
}

static void test_busy_frame_is_retained(void)
{
    memset(s_sent_by_pgn, 0, sizeof(s_sent_by_pgn));
    J1939_Sched_Clear();
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        assert(J1939_Sched_Add_Catalog_Signal(i, 0u, 0u));
    }
    J1939_Sched_Reset_Timers(0u);

    s_link_busy = true;
    J1939_Sched_Tick(3u, NULL);
    J1939_Sched_Stats_t stats;
    J1939_Sched_Get_Stats(&stats);
    assert(stats.queued > 0u);
    assert(stats.busy == 1u);
    assert(stats.sent == 0u);
    assert(stats.dropped == 0u);

    const uint8_t retained = stats.queued;
    s_link_busy = false;
    J1939_Sched_Tick(4u, NULL);
    J1939_Sched_Get_Stats(&stats);
    assert(stats.sent >= retained);
    assert(stats.queued == 0u);
    assert(stats.dropped == 0u);
}

int main(void)
{
    test_full_catalog_timing_and_payloads();
    test_busy_frame_is_retained();
    puts("Phase 3 PGN-centric scheduler test: PASS");
    return 0;
}
