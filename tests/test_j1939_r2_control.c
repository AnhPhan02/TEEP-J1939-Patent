#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_boot.h"
#include "app_control_can.h"
#include "app_generator.h"
#include "j1939_frame.h"
#include "j1939_link.h"
#include "j1939_rx_router.h"

#define TEST_RX_CAPACITY  16u

typedef struct {
    uint32_t can_id;
    uint8_t data[8];
    uint8_t data_length;
} Test_Rx_Frame_t;

static Test_Rx_Frame_t s_rx_frames[TEST_RX_CAPACITY];
static uint8_t s_rx_count;
static uint8_t s_rx_index;

static uint32_t s_tx_can_id;
static uint8_t s_tx_data[8];
static uint8_t s_tx_length;
static uint32_t s_tx_count;
static hal_can_tx_status_t s_next_tx_status;
static App_Set_Const_Status_t s_set_const_status;
static uint32_t s_set_const_count;
static uint16_t s_last_signal_key;
static uint32_t s_last_requested_raw;
static bool s_has_configured_signals;
static uint32_t s_start_count;
static uint32_t s_stop_count;
static bool s_is_running;
static J1939_Sched_Mode_t s_start_mode;
static uint32_t s_now_ms;
static bool s_txn_open;
static bool s_txn_rejected;
static uint16_t s_txn_expected;
static uint16_t s_txn_staged;
static uint16_t s_generation;

App_Set_Const_Status_t App_Gen_Set_Constant_Raw(uint16_t signal_key,
                                                uint32_t raw_value,
                                                uint32_t* applied_raw_value)
{
    s_set_const_count++;
    s_last_signal_key = signal_key;
    s_last_requested_raw = raw_value;
    if (s_set_const_status == APP_SET_CONST_OK) {
        s_has_configured_signals = true;
        s_generation++;
        if (applied_raw_value != NULL) {
            *applied_raw_value = raw_value;
        }
    }
    return s_set_const_status;
}

App_Config_Transaction_Status_t App_Gen_Begin_Config(uint16_t expected,
                                                     uint8_t expected_blobs)
{
    (void)expected_blobs;
    if (s_is_running) return APP_CONFIG_TXN_INVALID_STATE;
    if (s_txn_open) return APP_CONFIG_TXN_ALREADY_OPEN;
    s_txn_open = true;
    s_txn_rejected = false;
    s_txn_expected = expected;
    s_txn_staged = 0u;
    return APP_CONFIG_TXN_OK;
}

App_Config_Transaction_Status_t App_Gen_Begin_Profile_Config(
    uint16_t expected, uint8_t expected_blobs, uint8_t profile_id)
{
    (void)profile_id;
    return App_Gen_Begin_Config(expected, expected_blobs);
}

App_Config_Transaction_Status_t App_Gen_Stage_Constant_Raw(
    uint16_t signal_key, uint32_t raw_value, uint16_t* out_staged_count)
{
    (void)signal_key;
    (void)raw_value;
    if (!s_txn_open) return APP_CONFIG_TXN_NOT_OPEN;
    if (s_txn_rejected) return APP_CONFIG_TXN_REJECTED;
    s_txn_staged++;
    if (out_staged_count != NULL) *out_staged_count = s_txn_staged;
    return APP_CONFIG_TXN_OK;
}

App_Config_Transaction_Status_t App_Gen_Commit_Config(
    uint16_t* out_generation, uint16_t* out_signal_count,
    uint8_t* out_pgn_count)
{
    if (!s_txn_open) return APP_CONFIG_TXN_NOT_OPEN;
    s_txn_open = false;
    if (s_txn_rejected) return APP_CONFIG_TXN_REJECTED;
    if (s_txn_staged != s_txn_expected) return APP_CONFIG_TXN_COUNT_MISMATCH;
    s_generation++;
    s_has_configured_signals = s_txn_staged != 0u;
    if (out_generation != NULL) *out_generation = s_generation;
    if (out_signal_count != NULL) *out_signal_count = s_txn_staged;
    if (out_pgn_count != NULL) *out_pgn_count = s_txn_staged ? 1u : 0u;
    return APP_CONFIG_TXN_OK;
}

App_Config_Transaction_Status_t App_Gen_Stage_Blob_Chunk(
    uint16_t signal_key, uint8_t total_length, uint8_t offset,
    const uint8_t chunk[APP_BLOB_CHUNK_BYTES], uint16_t* out_written_bytes)
{
    (void)signal_key;
    (void)total_length;
    (void)offset;
    (void)chunk;
    if (out_written_bytes != NULL) *out_written_bytes = 0u;
    return APP_CONFIG_TXN_NOT_OPEN;
}

App_Config_Transaction_Status_t App_Gen_Stage_String_Chunk(
    uint16_t signal_key, uint8_t total_length, uint8_t offset,
    const uint8_t chunk[APP_BLOB_CHUNK_BYTES], uint16_t* out_written_bytes)
{
    return App_Gen_Stage_Blob_Chunk(signal_key, total_length, offset, chunk,
                                    out_written_bytes);
}

App_Config_Transaction_Status_t App_Gen_Set_Global_Percent(
    uint16_t percentage_bp, uint16_t* out_generation,
    uint16_t* out_affected_count, uint16_t* out_excluded_count)
{
    (void)percentage_bp;
    if (out_generation != NULL) *out_generation = s_generation;
    if (out_affected_count != NULL) *out_affected_count = 0u;
    if (out_excluded_count != NULL) *out_excluded_count = 0u;
    return APP_CONFIG_TXN_OK;
}

uint32_t App_Gen_Get_Config_Crc(void) { return 0u; }
uint8_t App_Gen_Get_Active_Profile_Id(void)
{
    return J1939_PROFILE_ID_CUSTOM;
}

void App_Gen_Abort_Config(void)
{
    s_txn_open = false;
    s_txn_rejected = false;
}

bool App_Gen_Config_Transaction_Open(void) { return s_txn_open; }
bool App_Gen_Config_Transaction_Rejected(void) { return s_txn_rejected; }
uint16_t App_Gen_Get_Config_Generation(void) { return s_generation; }
uint16_t App_Gen_Get_Active_Signal_Count(void)
{
    return s_has_configured_signals ? 1u : 0u;
}
uint8_t App_Gen_Get_Active_PGN_Count(void)
{
    return s_has_configured_signals ? 1u : 0u;
}
bool App_Gen_Active_Profile_Can_Start(void)
{
    return s_has_configured_signals && !s_txn_open;
}

bool App_Gen_Has_Configured_Signals(void)
{
    return s_has_configured_signals;
}

bool App_Gen_Is_Signal_Configured(uint16_t signal_key)
{
    return (signal_key == J1939_KEY_EEC1_SPN190) &&
           s_has_configured_signals;
}

void App_Gen_Start(uint32_t duration_sec, J1939_Sched_Mode_t mode)
{
    assert(duration_sec == 0u);
    s_start_count++;
    s_start_mode = mode;
    s_is_running = true;
}

void App_Gen_Stop(void)
{
    s_stop_count++;
    s_is_running = false;
}

/* R2 links app_control_can.c without app_boot.c (no generator). */
void App_Boot_Cancel_Autostart(void) {}
void App_Boot_Get_Info(App_Boot_Info_t* out) { memset(out, 0, sizeof(*out)); }
uint16_t App_Gen_Get_Bus_Recovery_Count(void) { return 0u; }

void App_Gen_Clear(void)
{
    s_has_configured_signals = false;
    s_is_running = false;
}

bool App_Gen_Is_Running(void)
{
    return s_is_running;
}

uint32_t hal_time_ms(void)
{
    return s_now_ms;
}

bool hal_can_init(uint32_t bitrate_bps)
{
    (void)bitrate_bps;
    return true;
}

bool hal_can_receive_ext(uint32_t* can_id, uint8_t* data, uint8_t* data_length)
{
    if (s_rx_index >= s_rx_count) {
        return false;
    }

    const Test_Rx_Frame_t* frame = &s_rx_frames[s_rx_index++];
    *can_id = frame->can_id;
    *data_length = frame->data_length;
    memcpy(data, frame->data, frame->data_length);
    return true;
}

hal_can_tx_status_t hal_can_send_ext(uint32_t can_id,
                                     const uint8_t* data,
                                     uint8_t data_length)
{
    s_tx_can_id = can_id;
    s_tx_length = data_length;
    memcpy(s_tx_data, data, data_length);
    s_tx_count++;
    return s_next_tx_status;
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

static void reset_fixture(void)
{
    memset(s_rx_frames, 0, sizeof(s_rx_frames));
    s_rx_count = 0u;
    s_rx_index = 0u;
    memset(s_tx_data, 0, sizeof(s_tx_data));
    s_tx_can_id = 0u;
    s_tx_length = 0u;
    s_tx_count = 0u;
    s_next_tx_status = HCAN_TX_OK;
    s_set_const_status = APP_SET_CONST_OK;
    s_set_const_count = 0u;
    s_last_signal_key = 0u;
    s_last_requested_raw = 0u;
    s_has_configured_signals = false;
    s_start_count = 0u;
    s_stop_count = 0u;
    s_is_running = false;
    s_start_mode = J1939_SCHED_MODE_SMOOTH;
    s_now_ms = 0u;
    s_txn_open = false;
    s_txn_rejected = false;
    s_txn_expected = 0u;
    s_txn_staged = 0u;
    s_generation = 0u;
    J1939_Rx_Router_Init();
    App_Control_Can_Init();
}

static void enqueue_frame(uint32_t can_id, const uint8_t* data, uint8_t length)
{
    assert(s_rx_count < TEST_RX_CAPACITY);
    assert(length <= 8u);
    Test_Rx_Frame_t* frame = &s_rx_frames[s_rx_count++];
    frame->can_id = can_id;
    frame->data_length = length;
    memcpy(frame->data, data, length);
}

static uint32_t response_can_id(void)
{
    return s_tx_can_id;
}

static void test_tc1001_golden_vector(void)
{
    static const uint8_t request[8] = {
        0x01u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected[8] = {
        0x81u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();
    enqueue_frame(0x18EF80F9u, request, sizeof(request));
    J1939_Rx_Router_Poll(3u);

    assert(s_tx_count == 1u);
    assert(response_can_id() == 0x18EFF980u);
    assert(s_tx_length == 8u);
    assert(memcmp(s_tx_data, expected, sizeof(expected)) == 0);
}

static void test_validation_statuses(void)
{
    uint8_t request[8] = {
        0x01u, 0x31u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();
    enqueue_frame(0x18EF80F9u, request, 7u);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_count == 1u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_BAD_DLC);
    assert(s_tx_data[3] == 7u);

    reset_fixture();
    request[0] = 0x55u;
    enqueue_frame(0x18EF80F9u, request, 8u);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_data[0] == 0xD5u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_UNSUPPORTED_OPCODE);

    reset_fixture();
    request[0] = APP_CONTROL_OPCODE_TC1001;
    request[1] = 0u;
    enqueue_frame(0x18EF80F9u, request, 8u);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_BAD_ARGUMENT);

    reset_fixture();
    request[1] = 0x32u;
    request[5] = 1u;
    enqueue_frame(0x18EF80F9u, request, 8u);
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_BAD_ARGUMENT);
    assert(s_tx_data[3] == 0u);
}

static void test_set_const_then_start(void)
{
    static const uint8_t set_const[8] = {
        0x10u, 0x02u, 0x67u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    static const uint8_t expected_set_response[8] = {
        0x90u, 0x02u, 0x00u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    static const uint8_t start[8] = {
        0x03u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected_start_response[8] = {
        0x83u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();
    enqueue_frame(0x18EF80F9u, set_const, sizeof(set_const));
    J1939_Rx_Router_Poll(3u);
    assert(s_last_signal_key == J1939_KEY_EEC1_SPN190);
    assert(s_last_requested_raw == 12000u);
    assert(memcmp(s_tx_data, expected_set_response,
                  sizeof(expected_set_response)) == 0);

    enqueue_frame(0x18EF80F9u, start, sizeof(start));
    J1939_Rx_Router_Poll(3u);
    assert(s_start_count == 1u);
    assert(s_start_mode == J1939_SCHED_MODE_SAE);
    assert(memcmp(s_tx_data, expected_start_response,
                  sizeof(expected_start_response)) == 0);
}

static void test_start_requires_a_configured_signal(void)
{
    static const uint8_t start[8] = {
        0x03u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();
    enqueue_frame(0x18EF80F9u, start, sizeof(start));
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_data[0] == 0x83u);
    assert(s_tx_data[1] == 0x04u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_INVALID_STATE);
    assert(s_start_count == 0u);
}

static void test_get_status_reports_runtime_and_protocol_info(void)
{
    static const uint8_t get_status_stopped[8] = {
        APP_CONTROL_OPCODE_GET_STATUS, 0x20u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected_stopped[8] = {
        0x84u, 0x20u, APP_CONTROL_STATUS_OK,
        0x00u, 0x02u, 0x05u, 0x01u, 0xFFu
    };
    static const uint8_t get_status_running[8] = {
        APP_CONTROL_OPCODE_GET_STATUS, 0x21u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected_running[8] = {
        0x84u, 0x21u, APP_CONTROL_STATUS_OK,
        (APP_CONTROL_RUNTIME_RUNNING |
         APP_CONTROL_RUNTIME_ENGINE_SPEED_CONFIGURED),
        0x02u, 0x05u, 0x01u, 0xFFu
    };

    reset_fixture();

    enqueue_frame(0x18EF80F9u, get_status_stopped,
                  sizeof(get_status_stopped));
    J1939_Rx_Router_Poll(3u);
    assert(memcmp(s_tx_data, expected_stopped, sizeof(expected_stopped)) == 0);

    s_is_running = true;
    s_has_configured_signals = true;
    enqueue_frame(0x18EF80F9u, get_status_running,
                  sizeof(get_status_running));
    J1939_Rx_Router_Poll(3u);
    assert(memcmp(s_tx_data, expected_running, sizeof(expected_running)) == 0);

    uint8_t invalid_status[8];
    memcpy(invalid_status, get_status_running, sizeof(invalid_status));
    invalid_status[1] = 0x22u;
    invalid_status[2] = APP_CONTROL_STATUS_PAGE_LAST + 1u;
    enqueue_frame(0x18EF80F9u, invalid_status, sizeof(invalid_status));
    J1939_Rx_Router_Poll(3u);
    assert(s_tx_data[0] == 0x84u);
    assert(s_tx_data[1] == 0x22u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_BAD_ARGUMENT);
}

static void test_stop_is_idempotent_and_validates_reserved_bytes(void)
{
    uint8_t stop[8] = {
        APP_CONTROL_OPCODE_STOP, 0x22u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected[8] = {
        0x82u, 0x22u, APP_CONTROL_STATUS_OK,
        0x00u,
        0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();
    s_is_running = true;
    enqueue_frame(0x18EF80F9u, stop, sizeof(stop));
    J1939_Rx_Router_Poll(3u);
    assert(s_stop_count == 1u);
    assert(!s_is_running);
    assert(memcmp(s_tx_data, expected, sizeof(expected)) == 0);

    enqueue_frame(0x18EF80F9u, stop, sizeof(stop));
    J1939_Rx_Router_Poll(3u);
    assert(s_stop_count == 1u);
    assert(memcmp(s_tx_data, expected, sizeof(expected)) == 0);

    stop[1] = 0x23u;
    stop[7] = 1u;
    enqueue_frame(0x18EF80F9u, stop, sizeof(stop));
    J1939_Rx_Router_Poll(3u);
    assert(s_stop_count == 1u);
    assert(s_tx_data[0] == 0x82u);
    assert(s_tx_data[1] == 0x23u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_BAD_ARGUMENT);
}

static void test_dedup_replay_conflict_and_expiry(void)
{
    uint8_t set_const[8] = {
        APP_CONTROL_OPCODE_SET_CONST, 0x60u,
        0x67u, 0x00u, 0xE0u, 0x2Eu, 0x00u, 0x00u
    };
    App_Control_Can_Stats_t stats;

    reset_fixture();
    enqueue_frame(0x18EF80F9u, set_const, sizeof(set_const));
    J1939_Rx_Router_Poll(3u);
    assert(s_set_const_count == 1u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_OK);

    enqueue_frame(0x18EF80F9u, set_const, sizeof(set_const));
    J1939_Rx_Router_Poll(3u);
    assert(s_set_const_count == 1u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_OK);
    App_Control_Can_Get_Stats(&stats);
    assert(stats.duplicate_replays == 1u);

    set_const[4]++;
    enqueue_frame(0x18EF80F9u, set_const, sizeof(set_const));
    J1939_Rx_Router_Poll(3u);
    assert(s_set_const_count == 1u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_DUPLICATE_CONFLICT);
    App_Control_Can_Get_Stats(&stats);
    assert(stats.duplicate_conflicts == 1u);

    s_now_ms = 2001u;
    enqueue_frame(0x18EF80F9u, set_const, sizeof(set_const));
    J1939_Rx_Router_Poll(3u);
    assert(s_set_const_count == 2u);
    assert(s_tx_data[2] == APP_CONTROL_STATUS_OK);
}

static void test_endpoint_filter_and_response_loop_guard(void)
{
    uint8_t request[8] = {
        0x01u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    reset_fixture();
    enqueue_frame(0x18EF81F9u, request, 8u); /* Wrong destination 0x81. */
    enqueue_frame(0x18EF80F8u, request, 8u); /* Wrong tester source. */
    enqueue_frame(0x14EF80F9u, request, 8u); /* Wrong priority 5. */
    request[0] = 0x81u;
    enqueue_frame(0x18EF80F9u, request, 8u); /* A response must not be answered. */
    J1939_Rx_Router_Poll(8u);

    assert(s_tx_count == 0u);
    App_Control_Can_Stats_t stats;
    App_Control_Can_Get_Stats(&stats);
    assert(stats.ignored_endpoint == 3u);
    assert(stats.ignored_response == 1u);
}

static void test_rx_budget_and_link_result_counters(void)
{
    static const uint8_t request[8] = {
        0x01u, 0x50u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    App_Control_Can_Stats_t stats;

    reset_fixture();
    enqueue_frame(0x18EF80F9u, request, 8u);
    enqueue_frame(0x18EF80F9u, request, 8u);
    s_next_tx_status = HCAN_TX_TIMEOUT;
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == 1u);
    assert(s_rx_index == 1u);
    App_Control_Can_Get_Stats(&stats);
    assert(stats.responses_busy == 1u);

    s_next_tx_status = HCAN_TX_ERROR;
    J1939_Rx_Router_Poll(1u);
    assert(s_rx_index == 2u);
    /* The duplicate response is already deferred, so it is not transmitted
     * again until the priority retry service runs. */
    s_now_ms = 1u;
    App_Control_Can_Process();
    App_Control_Can_Get_Stats(&stats);
    assert(stats.responses_failed == 1u);
}

static void test_busy_control_response_is_deferred_and_retried(void)
{
    static const uint8_t request[8] = {
        APP_CONTROL_OPCODE_TC1001, 0x70u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t expected[8] = {
        (APP_CONTROL_OPCODE_TC1001 | APP_CONTROL_RESPONSE_MASK), 0x70u,
        APP_CONTROL_STATUS_OK, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u
    };
    App_Control_Can_Stats_t stats;

    reset_fixture();
    s_next_tx_status = HCAN_TX_TIMEOUT;
    enqueue_frame(0x18EF80F9u, request, sizeof(request));
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == 1u);
    App_Control_Can_Get_Stats(&stats);
    assert(stats.responses_queued == 0u);
    assert(stats.responses_busy == 1u);
    assert(stats.responses_deferred == 1u);
    assert(stats.responses_dropped == 0u);

    /* Retry is deliberately delayed by one millisecond. */
    s_next_tx_status = HCAN_TX_OK;
    App_Control_Can_Process();
    assert(s_tx_count == 1u);
    s_now_ms = 1u;
    App_Control_Can_Process();
    assert(s_tx_count == 2u);
    assert(s_tx_can_id == 0x18EFF980u);
    assert(s_tx_length == sizeof(expected));
    assert(memcmp(s_tx_data, expected, sizeof(expected)) == 0);

    App_Control_Can_Get_Stats(&stats);
    assert(stats.responses_queued == 1u);
    assert(stats.responses_deferred == 1u);
    App_Control_Can_Process();
    assert(s_tx_count == 2u);
}

int main(void)
{
    test_tc1001_golden_vector();
    test_validation_statuses();
    test_set_const_then_start();
    test_start_requires_a_configured_signal();
    test_get_status_reports_runtime_and_protocol_info();
    test_stop_is_idempotent_and_validates_reserved_bytes();
    test_dedup_replay_conflict_and_expiry();
    test_endpoint_filter_and_response_loop_guard();
    test_rx_budget_and_link_result_counters();
    test_busy_control_response_is_deferred_and_retried();
    puts("R2 control protocol and dedup host tests: PASS");
    return 0;
}
