/* Phase 4A host integration test: proprietary CAN transaction protocol,
 * atomic scalar-bank commit, runtime status summary, and failure rollback. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_control_can.h"
#include "app_generator.h"
#include "hal_can.h"
#include "hal_console.h"
#include "hal_led.h"
#include "hal_time.h"
#include "j1939_rx_router.h"
#include "j1939_signal_definitions.h"
#include "j1939_value_store.h"

#define TEST_FRAME_CAPACITY  220u

typedef struct {
    uint32_t can_id;
    uint8_t data[8];
    uint8_t length;
} Test_Frame_t;

static Test_Frame_t s_rx[TEST_FRAME_CAPACITY];
static Test_Frame_t s_tx[TEST_FRAME_CAPACITY];
static uint16_t s_rx_count;
static uint16_t s_rx_index;
static uint16_t s_tx_count;
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

uint32_t hal_time_ms(void) { return s_now_ms; }
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

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void reset_fixture(void)
{
    memset(s_rx, 0, sizeof(s_rx));
    memset(s_tx, 0, sizeof(s_tx));
    s_rx_count = 0u;
    s_rx_index = 0u;
    s_tx_count = 0u;
    s_now_ms = 0u;
    App_Gen_Init(500u);
    J1939_Rx_Router_Init();
    App_Control_Can_Init();
}

static const uint8_t* send_request(const uint8_t request[8])
{
    assert(s_rx_count < TEST_FRAME_CAPACITY);
    Test_Frame_t* frame = &s_rx[s_rx_count++];
    frame->can_id = 0x18EF80F9u;
    frame->length = 8u;
    memcpy(frame->data, request, 8u);
    const uint16_t tx_before = s_tx_count;
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == (uint16_t)(tx_before + 1u));
    assert(s_tx[s_tx_count - 1u].can_id == 0x18EFF980u);
    return s_tx[s_tx_count - 1u].data;
}

static void make_begin(uint8_t frame[8], uint8_t sequence,
                       uint8_t expected_count)
{
    memset(frame, 0, 8u);
    frame[0] = APP_CONTROL_OPCODE_BEGIN_CONFIG;
    frame[1] = sequence;
    write_u32(&frame[2], J1939_CATALOG_CRC32);
    frame[6] = expected_count;
}

static void make_set_raw(uint8_t frame[8], uint8_t sequence,
                         uint16_t key, uint32_t raw)
{
    frame[0] = APP_CONTROL_OPCODE_SET_SIGNAL_RAW;
    frame[1] = sequence;
    frame[2] = (uint8_t)key;
    frame[3] = (uint8_t)(key >> 8);
    write_u32(&frame[4], raw);
}

static void make_empty_command(uint8_t frame[8], uint8_t opcode,
                               uint8_t sequence)
{
    memset(frame, 0, 8u);
    frame[0] = opcode;
    frame[1] = sequence;
}

static void test_failure_count_mismatch_and_abort_keep_active(void)
{
    uint8_t frame[8];
    reset_fixture();

    /* Establish an old committed profile through the compatible v1 command. */
    make_set_raw(frame, 1u, J1939_KEY_EEC1_SPN190, 12000u);
    frame[0] = APP_CONTROL_OPCODE_SET_CONST;
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Get_Config_Generation() == 1u);
    assert(App_Gen_Get_Active_Signal_Count() == 1u);

    make_begin(frame, 2u, 2u);
    frame[2] ^= 1u;
    const uint8_t* response = send_request(frame);
    assert(response[2] == APP_CONTROL_STATUS_CATALOG_MISMATCH);
    assert(read_u32(&response[4]) == J1939_CATALOG_CRC32);
    assert(!App_Gen_Config_Transaction_Open());

    make_begin(frame, 3u, 2u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);
    make_set_raw(frame, 4u, J1939_KEY_EEC2_SPN91, 1u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);

    /* Active bank remains readable while a replacement is staged. */
    uint32_t raw = 0u;
    const uint16_t engine_index =
        J1939_Catalog_Index_From_Key(J1939_KEY_EEC1_SPN190);
    assert(J1939_Value_Store_Get_Raw(engine_index, &raw));
    assert(raw == 12000u);

    make_set_raw(frame, 5u, 0x7FFFu, 0u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_UNKNOWN_SIGNAL_KEY);
    make_empty_command(frame, APP_CONTROL_OPCODE_COMMIT_CONFIG, 6u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_TRANSACTION_REJECTED);
    assert(App_Gen_Get_Config_Generation() == 1u);
    assert(App_Gen_Get_Active_Signal_Count() == 1u);
    assert(J1939_Value_Store_Get_Raw(engine_index, &raw) && raw == 12000u);

    make_begin(frame, 7u, 2u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);
    make_set_raw(frame, 8u, J1939_KEY_EEC2_SPN91, 1u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);
    make_empty_command(frame, APP_CONTROL_OPCODE_COMMIT_CONFIG, 9u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_COUNT_MISMATCH);
    assert(App_Gen_Get_Config_Generation() == 1u);

    make_begin(frame, 10u, 1u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);
    make_empty_command(frame, APP_CONTROL_OPCODE_ABORT_CONFIG, 11u);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);
    assert(!App_Gen_Config_Transaction_Open());
    assert(App_Gen_Get_Config_Generation() == 1u);
}

static void test_all_scalar_records_commit_once(void)
{
    uint8_t frame[8];
    bool pgn_seen[J1939_CATALOG_PGN_COUNT];
    memset(pgn_seen, 0, sizeof(pgn_seen));
    uint16_t scalar_count = 0u;
    uint8_t pgn_count = 0u;

    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        if ((sig->flags & J1939_CAT_SIG_BLOB) != 0u) {
            continue;
        }
        assert(sig->bit_length <= 32u);
        scalar_count++;
        if (!pgn_seen[sig->pgn_index]) {
            pgn_seen[sig->pgn_index] = true;
            pgn_count++;
        }
    }
    assert(scalar_count == 178u); /* VIN is the one WRITE_BLOB record. */
    assert(pgn_count == 59u);

    reset_fixture();
    make_begin(frame, 1u, (uint8_t)scalar_count);
    const uint8_t* response = send_request(frame);
    assert(response[2] == APP_CONTROL_STATUS_OK);
    assert(response[3] == scalar_count);
    assert(read_u32(&response[4]) == J1939_CATALOG_CRC32);

    uint16_t staged = 0u;
    uint8_t sequence = 2u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        if ((sig->flags & J1939_CAT_SIG_BLOB) != 0u) {
            continue;
        }
        make_set_raw(frame, sequence++, sig->signal_key, sig->raw_min);
        response = send_request(frame);
        staged++;
        assert(response[2] == APP_CONTROL_STATUS_OK);
        assert(response[3] == (uint8_t)staged);
        assert(read_u32(&response[4]) == sig->raw_min);
    }
    assert(App_Gen_Get_Config_Generation() == 0u);
    assert(App_Gen_Get_Active_Signal_Count() == 0u);

    make_empty_command(frame, APP_CONTROL_OPCODE_COMMIT_CONFIG, sequence++);
    response = send_request(frame);
    assert(response[2] == APP_CONTROL_STATUS_OK);
    assert(response[3] == pgn_count);
    assert((read_u32(&response[4]) & 0xFFFFu) == 1u);
    assert((read_u32(&response[4]) >> 16) == scalar_count);
    assert(App_Gen_Get_Config_Generation() == 1u);
    assert(App_Gen_Get_Active_Signal_Count() == scalar_count);
    assert(App_Gen_Get_Active_PGN_Count() == pgn_count);
    assert(!App_Gen_Config_Transaction_Open());

    /* Exact retry with the same sequence replays the cached ACK; it must not
     * attempt a second COMMIT or increment generation. */
    uint8_t first_commit_response[8];
    memcpy(first_commit_response, response, sizeof(first_commit_response));
    response = send_request(frame);
    assert(memcmp(response, first_commit_response,
                  sizeof(first_commit_response)) == 0);
    assert(App_Gen_Get_Config_Generation() == 1u);

    /* Runtime page packs generation, active signal count and active PGN count. */
    make_empty_command(frame, APP_CONTROL_OPCODE_GET_STATUS, sequence++);
    frame[2] = APP_CONTROL_STATUS_PAGE_RUNTIME;
    response = send_request(frame);
    assert(response[2] == APP_CONTROL_STATUS_OK);
    assert((response[3] & APP_CONTROL_RUNTIME_COMMITTED) != 0u);
    assert((read_u32(&response[4]) & 0xFFFFu) == 1u);
    assert(((read_u32(&response[4]) >> 16) & 0xFFu) == scalar_count);
    assert((read_u32(&response[4]) >> 24) == pgn_count);

    /* The three multi-packet records (FEE1/FEE3) are broadcast with BAM. */
    make_empty_command(frame, APP_CONTROL_OPCODE_START, sequence++);
    response = send_request(frame);
    assert(response[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Is_Running());
    make_empty_command(frame, APP_CONTROL_OPCODE_STOP, sequence++);
    assert(send_request(frame)[2] == APP_CONTROL_STATUS_OK);
}

int main(void)
{
    test_failure_count_mismatch_and_abort_keep_active();
    test_all_scalar_records_commit_once();
    puts("Phase 4A transactional control host tests: PASS (178 scalar records)");
    return 0;
}
