#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_generator.h"
#include "j1939_signal_definitions_legacy.h"
#include "j1939_value_store.h"
#include "hal_console.h"
#include "hal_led.h"
#include "hal_time.h"
#include "j1939_encode_decode.h"
#include "j1939_link.h"

#define EXPECTED_PGN_COUNT  11u

typedef struct {
    uint32_t pgn;
    uint16_t expected_per_second;
    uint16_t sent;
    uint8_t payload[8];
    uint8_t priority;
    uint8_t source_address;
} Captured_PGN_t;

static Captured_PGN_t s_capture[EXPECTED_PGN_COUNT] = {
    { PGN_EBS11, 100u, 0u, {0u}, 0u, 0u },
    { PGN_ETC1,  100u, 0u, {0u}, 0u, 0u },
    { PGN_VDC2,  100u, 0u, {0u}, 0u, 0u },
    { PGN_EAC2,   50u, 0u, {0u}, 0u, 0u },
    { PGN_HRW,    50u, 0u, {0u}, 0u, 0u },
    { PGN_EEC2,   20u, 0u, {0u}, 0u, 0u },
    { PGN_TCO1,   20u, 0u, {0u}, 0u, 0u },
    { PGN_ERC1,   10u, 0u, {0u}, 0u, 0u },
    { PGN_EBC1,   10u, 0u, {0u}, 0u, 0u },
    { PGN_FLI2,   10u, 0u, {0u}, 0u, 0u },
    { PGN_PTODE,  10u, 0u, {0u}, 0u, 0u }
};

static uint32_t s_now_ms;
static bool s_link_busy;

static Captured_PGN_t* find_capture(uint32_t pgn)
{
    for (uint8_t i = 0u; i < EXPECTED_PGN_COUNT; i++) {
        if (s_capture[i].pgn == pgn) {
            return &s_capture[i];
        }
    }
    return NULL;
}

static void reset_capture(void)
{
    for (uint8_t i = 0u; i < EXPECTED_PGN_COUNT; i++) {
        s_capture[i].sent = 0u;
        memset(s_capture[i].payload, 0, sizeof(s_capture[i].payload));
        s_capture[i].priority = 0u;
        s_capture[i].source_address = 0u;
    }
    s_link_busy = false;
}

bool J1939_Link_Init(uint32_t baud_kbps)
{
    (void)baud_kbps;
    return true;
}

J1939_Link_Status_t J1939_Link_Send(uint32_t pgn,
                                    const uint8_t* data,
                                    uint8_t data_length,
                                    uint8_t priority,
                                    uint8_t source_address)
{
    if (s_link_busy) {
        return J1939_LINK_BUSY;
    }
    assert(data_length == 8u);
    Captured_PGN_t* capture = find_capture(pgn);
    assert(capture != NULL);
    capture->sent++;
    memcpy(capture->payload, data, 8u);
    capture->priority = priority;
    capture->source_address = source_address;
    return J1939_LINK_OK;
}

void J1939_Link_Get_Bit_Timing(J1939_Link_Bit_Timing_t* out)
{
    memset(out, 0, sizeof(*out));
}

void J1939_Link_Get_Bus_Error(J1939_Link_Bus_Error_t* out)
{
    memset(out, 0, sizeof(*out));
}

uint32_t hal_time_ms(void)
{
    return s_now_ms;
}

void hal_led_set(bool on) { (void)on; }
void hal_console_write(const char* str) { (void)str; }
void hal_console_write_line(const char* str) { (void)str; }
void hal_console_write_u32(uint32_t value) { (void)value; }
void hal_console_write_i32(int32_t value) { (void)value; }
void hal_console_write_hex(uint32_t value) { (void)value; }
void hal_console_write_float(float value, uint8_t decimals)
{
    (void)value;
    (void)decimals;
}
uint16_t hal_console_tx_free(void) { return UINT16_MAX; }

static void configure_profile_raw_one(void)
{
    assert(J1939_UI_Profile_40_Key_Count == J1939_UI_PROFILE_40_SIGNAL_COUNT);
    for (uint16_t i = 0u; i < J1939_UI_Profile_40_Key_Count; i++) {
        for (uint16_t j = 0u; j < i; j++) {
            assert(J1939_UI_Profile_40_Keys[i] !=
                   J1939_UI_Profile_40_Keys[j]);
        }
        uint32_t applied_raw = 0u;
        assert(App_Gen_Set_Constant_Raw(J1939_UI_Profile_40_Keys[i], 1u,
                                        &applied_raw) == APP_SET_CONST_OK);
        assert(applied_raw == 1u);
    }
    assert(J1939_Sched_Get_Signal_Count() == 40u);
    assert(J1939_Sched_Get_PGN_Count() == EXPECTED_PGN_COUNT);
    assert(Pattern_Generator_Get_Count() == 0u);
    assert(J1939_Value_Store_Get_Active_Count() == 40u);

    /* A 41st signal is rejected before either runtime table is mutated. */
    const uint16_t generation = J1939_Value_Store_Get_Generation();
    assert(App_Gen_Set_Constant_Raw(J1939_SIGNAL_KEY_ENGINE_SPEED, 1u,
                                    NULL) == APP_SET_CONST_RESOURCE_FULL);
    assert(J1939_Value_Store_Get_Generation() == generation);
    assert(Pattern_Generator_Get_Count() == 0u);
    assert(J1939_Value_Store_Get_Active_Count() == 40u);
    assert(J1939_Sched_Get_Signal_Count() == 40u);
    assert(J1939_Sched_Get_PGN_Count() == EXPECTED_PGN_COUNT);
}

static void assert_all_payloads_decode_to_raw_one(void)
{
    for (uint16_t i = 0u; i < J1939_UI_Profile_40_Key_Count; i++) {
        const J1939_Signal_Definition_t* def =
            J1939_Find_Signal_By_Key(J1939_UI_Profile_40_Keys[i]);
        Captured_PGN_t* capture;
        float decoded = 0.0f;

        assert(def != NULL);
        capture = find_capture(def->pgn);
        assert(capture != NULL && capture->sent > 0u);
        assert(J1939_Decode_Signal_Sized(def, capture->payload, 8u,
                                         &decoded));
        const float expected = def->resolution + def->offset;
        const float tolerance = fabsf(def->resolution) * 0.01f + 0.000001f;
        assert(fabsf(decoded - expected) <= tolerance);
    }
}

static void test_profile_keys_periods_and_payloads(void)
{
    reset_capture();
    s_now_ms = 0u;
    App_Gen_Init(250u);
    configure_profile_raw_one();
    App_Gen_Start(0u, J1939_SCHED_MODE_SAE);

    for (s_now_ms = 1u; s_now_ms <= 1000u; s_now_ms++) {
        App_Gen_Process(s_now_ms);
    }

    uint32_t total = 0u;
    for (uint8_t i = 0u; i < EXPECTED_PGN_COUNT; i++) {
        assert(s_capture[i].sent == s_capture[i].expected_per_second);
        total += s_capture[i].sent;
    }
    assert(total == 480u);
    assert(App_Gen_Get_Frames_Sent() == 480u);
    assert_all_payloads_decode_to_raw_one();

    J1939_Sched_Stats_t stats;
    J1939_Sched_Get_Stats(&stats);
    assert(stats.enqueued == 480u);
    assert(stats.sent == 480u);
    assert(stats.busy == 0u);
    assert(stats.failed == 0u);
    assert(stats.dropped == 0u);
    assert(stats.deadline_missed == 0u);
    /* Phase offsets prevent the former 11-frame synchronized startup burst. */
    assert(stats.high_water > 0u);
    assert(stats.high_water < EXPECTED_PGN_COUNT);
}

static void test_eleven_frame_burst_is_retained_while_busy(void)
{
    reset_capture();
    s_now_ms = 0u;
    App_Gen_Init(250u);
    configure_profile_raw_one();
    App_Gen_Start(0u, J1939_SCHED_MODE_SAE);

    s_link_busy = true;
    s_now_ms = 100u;
    App_Gen_Process(s_now_ms);

    J1939_Sched_Stats_t stats;
    J1939_Sched_Get_Stats(&stats);
    assert(stats.enqueued == EXPECTED_PGN_COUNT);
    assert(stats.queued == EXPECTED_PGN_COUNT);
    assert(stats.high_water == EXPECTED_PGN_COUNT);
    assert(stats.busy == 1u);
    assert(stats.dropped == 0u);

    s_link_busy = false;
    s_now_ms = 101u;
    App_Gen_Process(s_now_ms);
    J1939_Sched_Get_Stats(&stats);
    assert(stats.queued == 0u);
    assert(stats.sent == EXPECTED_PGN_COUNT);
    assert(stats.dropped == 0u);
}

static void test_transaction_capacity_matches_profile_40(void)
{
    reset_capture();
    App_Gen_Init(250u);
    assert(App_Gen_Begin_Config(41u, 0u) == APP_CONFIG_TXN_RESOURCE_FULL);
    assert(!App_Gen_Config_Transaction_Open());
    assert(App_Gen_Begin_Config(40u, 0u) == APP_CONFIG_TXN_OK);
    assert(App_Gen_Config_Transaction_Open());
    App_Gen_Abort_Config();
    assert(!App_Gen_Config_Transaction_Open());
}

int main(void)
{
    test_profile_keys_periods_and_payloads();
    test_eleven_frame_burst_is_retained_while_busy();
    test_transaction_capacity_matches_profile_40();
    puts("40-SPN / 11-PGN scheduler integration test: PASS");
    return 0;
}
