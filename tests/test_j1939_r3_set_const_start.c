#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_control_can.h"
#include "app_generator.h"
#include "j1939_signal_definitions_legacy.h"
#include "j1939_value_store.h"
#include "hal_can.h"
#include "hal_console.h"
#include "hal_led.h"
#include "hal_time.h"
#include "j1939_rx_router.h"

#define TEST_FRAME_CAPACITY  96u

typedef struct {
    uint32_t can_id;
    uint8_t data[8];
    uint8_t length;
} Test_Frame_t;

static Test_Frame_t s_rx[TEST_FRAME_CAPACITY];
static Test_Frame_t s_tx[TEST_FRAME_CAPACITY];
static uint8_t s_rx_count;
static uint8_t s_rx_index;
static uint8_t s_tx_count;
static uint32_t s_now_ms;

bool hal_can_init(uint32_t bitrate_bps)
{
    (void)bitrate_bps;
    return true;
}

bool hal_can_receive_ext(uint32_t* can_id, uint8_t* data, uint8_t* length)
{
    if (s_rx_index >= s_rx_count) {
        return false;
    }
    const Test_Frame_t* frame = &s_rx[s_rx_index++];
    *can_id = frame->can_id;
    *length = frame->length;
    memcpy(data, frame->data, frame->length);
    return true;
}

hal_can_tx_status_t hal_can_send_ext(uint32_t can_id,
                                     const uint8_t* data,
                                     uint8_t length)
{
    assert(s_tx_count < TEST_FRAME_CAPACITY);
    Test_Frame_t* frame = &s_tx[s_tx_count++];
    frame->can_id = can_id;
    frame->length = length;
    memcpy(frame->data, data, length);
    return HCAN_TX_OK;
}

void hal_can_get_bit_timing(hal_can_bit_timing_t* out)
{
    memset(out, 0, sizeof(*out));
}

void hal_can_get_error(hal_can_error_t* out)
{
    memset(out, 0, sizeof(*out));
}

void hal_can_diagnose(hal_can_diag_t* out)
{
    memset(out, 0, sizeof(*out));
}

uint32_t hal_time_ms(void)
{
    return s_now_ms;
}

void hal_led_set(bool on)
{
    (void)on;
}

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

static void enqueue_rx(uint32_t can_id, const uint8_t data[8])
{
    assert(s_rx_count < TEST_FRAME_CAPACITY);
    Test_Frame_t* frame = &s_rx[s_rx_count++];
    frame->can_id = can_id;
    frame->length = 8u;
    memcpy(frame->data, data, 8u);
}

static void reset_fixture(void)
{
    memset(s_rx, 0, sizeof(s_rx));
    memset(s_tx, 0, sizeof(s_tx));
    s_rx_count = 0u;
    s_rx_index = 0u;
    s_tx_count = 0u;
    s_now_ms = 0u;

    App_Gen_Init(250u);
    J1939_Rx_Router_Init();
    App_Control_Can_Init();
}

static void test_set_const_start_and_periodic_eec1(void)
{
    static const uint8_t set_const[8] = {
        0x10u, 0x02u, 0x67u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    static const uint8_t set_response[8] = {
        0x90u, 0x02u, 0x00u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    static const uint8_t start[8] = {
        0x03u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t start_response[8] = {
        0x83u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t eec1_1500_rpm[8] = {
        0xFFu, 0xFFu, 0xFFu, 0xE0u, 0x2Eu, 0xFFu, 0xFFu, 0xFFu
    };

    reset_fixture();

    enqueue_rx(0x18EF80F9u, set_const);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_count == 1u);
    assert(s_tx[0].can_id == 0x18EFF980u);
    assert(memcmp(s_tx[0].data, set_response, 8u) == 0);

    enqueue_rx(0x18EF80F9u, start);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_count == 2u);
    assert(s_tx[1].can_id == 0x18EFF980u);
    assert(memcmp(s_tx[1].data, start_response, 8u) == 0);

    /* EEC1 broadcasts every 5000 ms (customer demo period). */
    s_now_ms = 4999u;
    App_Gen_Process(s_now_ms);
    assert(s_tx_count == 2u);

    s_now_ms = 5000u;
    App_Gen_Process(s_now_ms);
    assert(s_tx_count == 3u);
    assert(s_tx[2].can_id == 0x0CF00400u);
    assert(memcmp(s_tx[2].data, eec1_1500_rpm, 8u) == 0);

    s_now_ms = 9999u;
    App_Gen_Process(s_now_ms);
    assert(s_tx_count == 3u);

    s_now_ms = 10000u;
    App_Gen_Process(s_now_ms);
    assert(s_tx_count == 4u);
    assert(s_tx[3].can_id == 0x0CF00400u);
    assert(memcmp(s_tx[3].data, eec1_1500_rpm, 8u) == 0);
}

static void test_set_const_validation(void)
{
    static const uint8_t unknown_key[8] = {
        0x10u, 0x10u, 0xFFu, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t out_of_range[8] = {
        0x10u, 0x11u, 0x67u, 0x00u, 0xFFu, 0xFFu, 0x00u, 0x00u
    };

    reset_fixture();
    enqueue_rx(0x18EF80F9u, unknown_key);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_count == 1u);
    assert(s_tx[0].data[0] == 0x90u);
    assert(s_tx[0].data[1] == 0x10u);
    assert(s_tx[0].data[2] == APP_CONTROL_STATUS_UNKNOWN_SIGNAL_KEY);

    enqueue_rx(0x18EF80F9u, out_of_range);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_count == 2u);
    assert(s_tx[1].data[0] == 0x90u);
    assert(s_tx[1].data[1] == 0x11u);
    assert(s_tx[1].data[2] == APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE);
}

static void test_get_status_and_stop_flow(void)
{
    static const uint8_t set_const[8] = {
        0x10u, 0x30u, 0x67u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    static const uint8_t start[8] = {
        APP_CONTROL_OPCODE_START, 0x31u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t get_status_running[8] = {
        APP_CONTROL_OPCODE_GET_STATUS, 0x32u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected_running[8] = {
        0x84u, 0x32u, APP_CONTROL_STATUS_OK,
        (APP_CONTROL_RUNTIME_RUNNING |
         APP_CONTROL_RUNTIME_ENGINE_SPEED_CONFIGURED |
         APP_CONTROL_RUNTIME_COMMITTED),
        0x02u, 0x05u, 0x01u, 0xFFu
    };
    static const uint8_t stop[8] = {
        APP_CONTROL_OPCODE_STOP, 0x33u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected_stop[8] = {
        0x82u, 0x33u, APP_CONTROL_STATUS_OK,
        0x00u,
        0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t get_status_stopped[8] = {
        APP_CONTROL_OPCODE_GET_STATUS, 0x34u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected_stopped[8] = {
        0x84u, 0x34u, APP_CONTROL_STATUS_OK,
        (APP_CONTROL_RUNTIME_ENGINE_SPEED_CONFIGURED |
         APP_CONTROL_RUNTIME_COMMITTED),
        0x02u, 0x05u, 0x01u, 0xFFu
    };

    reset_fixture();
    enqueue_rx(0x18EF80F9u, set_const);
    enqueue_rx(0x18EF80F9u, start);
    J1939_Rx_Router_Poll(3u);
    assert(App_Gen_Is_Running());

    s_now_ms = 5000u;
    App_Gen_Process(s_now_ms);
    assert(App_Gen_Get_Frames_Sent() == 1u);

    enqueue_rx(0x18EF80F9u, get_status_running);
    J1939_Rx_Router_Poll(3u);
    assert(memcmp(s_tx[3].data, expected_running, 8u) == 0);

    enqueue_rx(0x18EF80F9u, stop);
    J1939_Rx_Router_Poll(3u);
    assert(!App_Gen_Is_Running());
    assert(memcmp(s_tx[4].data, expected_stop, 8u) == 0);

    enqueue_rx(0x18EF80F9u, get_status_stopped);
    J1939_Rx_Router_Poll(3u);
    assert(memcmp(s_tx[5].data, expected_stopped, 8u) == 0);

    s_now_ms = 10000u;
    App_Gen_Process(s_now_ms);
    assert(s_tx_count == 6u);
}

static void test_clear_config_flow(void)
{
    static const uint8_t set_const[8] = {
        APP_CONTROL_OPCODE_SET_CONST, 0x40u,
        0x67u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    static const uint8_t start[8] = {
        APP_CONTROL_OPCODE_START, 0x41u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t clear_while_running[8] = {
        APP_CONTROL_OPCODE_CLEAR_CONFIG, 0x42u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t stop[8] = {
        APP_CONTROL_OPCODE_STOP, 0x43u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t clear_stopped[8] = {
        APP_CONTROL_OPCODE_CLEAR_CONFIG, 0x44u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t start_empty[8] = {
        APP_CONTROL_OPCODE_START, 0x45u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();
    enqueue_rx(0x18EF80F9u, set_const);
    enqueue_rx(0x18EF80F9u, start);
    enqueue_rx(0x18EF80F9u, clear_while_running);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_count == 3u);
    assert(s_tx[2].data[0] ==
           (APP_CONTROL_OPCODE_CLEAR_CONFIG | APP_CONTROL_RESPONSE_MASK));
    assert(s_tx[2].data[2] == APP_CONTROL_STATUS_INVALID_STATE);
    assert(App_Gen_Is_Running());
    assert(App_Gen_Has_Configured_Signals());

    enqueue_rx(0x18EF80F9u, stop);
    enqueue_rx(0x18EF80F9u, clear_stopped);
    enqueue_rx(0x18EF80F9u, start_empty);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_count == 6u);
    assert(s_tx[4].data[0] ==
           (APP_CONTROL_OPCODE_CLEAR_CONFIG | APP_CONTROL_RESPONSE_MASK));
    assert(s_tx[4].data[2] == APP_CONTROL_STATUS_OK);
    assert(!App_Gen_Has_Configured_Signals());
    assert(s_tx[5].data[2] == APP_CONTROL_STATUS_INVALID_STATE);
    assert(!App_Gen_Is_Running());
}

static void test_ui_replaces_legacy_signal_with_profile_40(void)
{
    static const uint8_t legacy_engine_speed[8] = {
        APP_CONTROL_OPCODE_SET_CONST, 0x48u,
        0x67u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    static const uint8_t clear_config[8] = {
        APP_CONTROL_OPCODE_CLEAR_CONFIG, 0x49u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t start[8] = {
        APP_CONTROL_OPCODE_START, 0x78u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();

    /* Reproduce the user's current session: one legacy EEC1 signal exists. */
    enqueue_rx(0x18EF80F9u, legacy_engine_speed);
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == 1u);
    assert(s_tx[0].data[2] == APP_CONTROL_STATUS_OK);
    assert(J1939_Sched_Get_Signal_Count() == 1u);

    enqueue_rx(0x18EF80F9u, clear_config);
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == 2u);
    assert(s_tx[1].data[2] == APP_CONTROL_STATUS_OK);
    assert(J1939_Sched_Get_Signal_Count() == 0u);

    for (uint16_t i = 0u; i < J1939_UI_Profile_40_Key_Count; i++) {
        uint8_t set_const[8] = {
            APP_CONTROL_OPCODE_SET_CONST,
            (uint8_t)(0x50u + i),
            (uint8_t)J1939_UI_Profile_40_Keys[i],
            (uint8_t)(J1939_UI_Profile_40_Keys[i] >> 8),
            0x01u, 0x00u, 0x00u, 0x00u
        };
        enqueue_rx(0x18EF80F9u, set_const);
        J1939_Rx_Router_Poll(1u);
        assert(s_tx_count == (uint8_t)(3u + i));
        assert(s_tx[s_tx_count - 1u].data[0] ==
               (APP_CONTROL_OPCODE_SET_CONST | APP_CONTROL_RESPONSE_MASK));
        assert(s_tx[s_tx_count - 1u].data[1] == set_const[1]);
        assert(s_tx[s_tx_count - 1u].data[2] == APP_CONTROL_STATUS_OK);
    }

    /* Constants live in the value store, not in the waveform pool. */
    assert(Pattern_Generator_Get_Count() == 0u);
    assert(J1939_Value_Store_Get_Active_Count() == J1939_UI_PROFILE_40_SIGNAL_COUNT);
    assert(J1939_Sched_Get_Signal_Count() == J1939_UI_PROFILE_40_SIGNAL_COUNT);
    assert(J1939_Sched_Get_PGN_Count() == 11u);

    enqueue_rx(0x18EF80F9u, start);
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == 43u);
    assert(s_tx[42].data[0] ==
           (APP_CONTROL_OPCODE_START | APP_CONTROL_RESPONSE_MASK));
    assert(s_tx[42].data[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Is_Running());
}

int main(void)
{
    test_set_const_start_and_periodic_eec1();
    test_set_const_validation();
    test_get_status_and_stop_flow();
    test_clear_config_flow();
    test_ui_replaces_legacy_signal_with_profile_40();
    puts("R3 SET_CONST/START/GET_STATUS/STOP/CLEAR integration test: PASS");
    return 0;
}
