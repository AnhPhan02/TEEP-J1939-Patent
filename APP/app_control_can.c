/*
 * =============================================================================
 * FILE : app_control_can.c
 * WHAT : Decode UI control requests and return application-level responses.
 * =============================================================================
 */

#include "app_control_can.h"
#include "app_generator.h"
#include "hal_time.h"
#include "j1939_link.h"
#include "j1939_rx_router.h"
#include "j1939_value_store.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define APP_CONTROL_DEDUP_CACHE_CAPACITY  16u
#define APP_CONTROL_DEDUP_WINDOW_MS       2000u
#define APP_CONTROL_TX_QUEUE_CAPACITY      4u
#define APP_CONTROL_TX_RETRY_DELAY_MS      1u

typedef struct {
    bool valid;
    uint8_t source_address;
    uint8_t request[APP_CONTROL_FRAME_LENGTH];
    uint8_t response[APP_CONTROL_FRAME_LENGTH];
    uint32_t timestamp_ms;
} App_Control_Cache_Entry_t;

typedef enum {
    APP_CONTROL_CACHE_MISS = 0,
    APP_CONTROL_CACHE_REPLAY,
    APP_CONTROL_CACHE_CONFLICT
} App_Control_Cache_Result_t;

typedef struct {
    uint8_t response[APP_CONTROL_FRAME_LENGTH];
    uint32_t queued_ms;
} App_Control_Tx_Entry_t;

static App_Control_Can_Stats_t s_stats;
static App_Control_Cache_Entry_t s_cache[APP_CONTROL_DEDUP_CACHE_CAPACITY];
static uint8_t s_cache_next;
static App_Control_Tx_Entry_t s_tx_queue[APP_CONTROL_TX_QUEUE_CAPACITY];
static uint8_t s_tx_queue_head;
static uint8_t s_tx_queue_count;
static uint32_t s_tx_retry_not_before_ms;

static uint16_t prv_read_u16_le(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t prv_read_u32_le(const uint8_t* data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void prv_write_u32_le(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void prv_build_response(uint8_t* response,
                               uint8_t request_opcode,
                               uint8_t sequence,
                               App_Control_Status_t status,
                               uint8_t detail,
                               uint32_t value)
{
    response[0] = (uint8_t)(request_opcode | APP_CONTROL_RESPONSE_MASK);
    response[1] = sequence;
    response[2] = (uint8_t)status;
    response[3] = detail;
    prv_write_u32_le(&response[4], value);
}

static bool prv_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static J1939_Link_Status_t prv_transmit_response(const uint8_t* response)
{
    return J1939_Link_Send_To(
        J1939_PGN_PROPRIETARY_A_CONTROL,
        response,
        APP_CONTROL_FRAME_LENGTH,
        APP_CONTROL_PRIORITY,
        APP_CONTROL_STM32_ADDRESS,
        APP_CONTROL_TESTER_ADDRESS);
}

static void prv_pop_tx_queue(void)
{
    if (s_tx_queue_count == 0u) {
        return;
    }
    s_tx_queue_head = (uint8_t)((s_tx_queue_head + 1u) %
                                APP_CONTROL_TX_QUEUE_CAPACITY);
    s_tx_queue_count--;
}

static bool prv_tx_queue_contains(const uint8_t* response)
{
    for (uint8_t i = 0u; i < s_tx_queue_count; i++) {
        const uint8_t index = (uint8_t)((s_tx_queue_head + i) %
                                        APP_CONTROL_TX_QUEUE_CAPACITY);
        if (memcmp(s_tx_queue[index].response, response,
                   APP_CONTROL_FRAME_LENGTH) == 0) {
            return true;
        }
    }
    return false;
}

static void prv_defer_response(const uint8_t* response, uint32_t now_ms)
{
    /* The UI permits only one outstanding command, but keep a small queue so
     * diagnostics or retries cannot overwrite an earlier ACK. A retry of the
     * exact same command must not create duplicate queued responses. */
    if (prv_tx_queue_contains(response)) {
        return;
    }
    if (s_tx_queue_count >= APP_CONTROL_TX_QUEUE_CAPACITY) {
        s_stats.responses_dropped++;
        return;
    }

    const uint8_t tail = (uint8_t)((s_tx_queue_head + s_tx_queue_count) %
                                   APP_CONTROL_TX_QUEUE_CAPACITY);
    memcpy(s_tx_queue[tail].response, response, APP_CONTROL_FRAME_LENGTH);
    s_tx_queue[tail].queued_ms = now_ms;
    s_tx_queue_count++;
    s_stats.responses_deferred++;
}

static void prv_send_response(const uint8_t* response)
{
    const uint32_t now_ms = hal_time_ms();

    /* Preserve response ordering once a response has been deferred. */
    if (s_tx_queue_count > 0u) {
        prv_defer_response(response, now_ms);
        return;
    }

    const J1939_Link_Status_t link_status = prv_transmit_response(response);

    if (link_status == J1939_LINK_OK) {
        s_stats.responses_queued++;
    } else if (link_status == J1939_LINK_BUSY) {
        s_stats.responses_busy++;
        prv_defer_response(response, now_ms);
        s_tx_retry_not_before_ms = now_ms + APP_CONTROL_TX_RETRY_DELAY_MS;
    } else {
        s_stats.responses_failed++;
        prv_defer_response(response, now_ms);
        s_tx_retry_not_before_ms = now_ms + APP_CONTROL_TX_RETRY_DELAY_MS;
    }
}

static App_Control_Cache_Result_t prv_cache_lookup(
    uint8_t source_address,
    const uint8_t* request,
    uint32_t now_ms,
    uint8_t* cached_response)
{
    for (uint8_t i = 0u; i < APP_CONTROL_DEDUP_CACHE_CAPACITY; i++) {
        App_Control_Cache_Entry_t* entry = &s_cache[i];
        if (!entry->valid) {
            continue;
        }
        if ((now_ms - entry->timestamp_ms) > APP_CONTROL_DEDUP_WINDOW_MS) {
            entry->valid = false;
            continue;
        }
        if ((entry->source_address != source_address) ||
            (entry->request[1] != request[1])) {
            continue;
        }
        if (memcmp(entry->request, request, APP_CONTROL_FRAME_LENGTH) == 0) {
            memcpy(cached_response, entry->response,
                   APP_CONTROL_FRAME_LENGTH);
            s_stats.duplicate_replays++;
            return APP_CONTROL_CACHE_REPLAY;
        }
        s_stats.duplicate_conflicts++;
        return APP_CONTROL_CACHE_CONFLICT;
    }
    return APP_CONTROL_CACHE_MISS;
}

static void prv_cache_store(uint8_t source_address,
                            const uint8_t* request,
                            const uint8_t* response,
                            uint32_t now_ms)
{
    App_Control_Cache_Entry_t* entry = &s_cache[s_cache_next];
    entry->valid = true;
    entry->source_address = source_address;
    memcpy(entry->request, request, APP_CONTROL_FRAME_LENGTH);
    memcpy(entry->response, response, APP_CONTROL_FRAME_LENGTH);
    entry->timestamp_ms = now_ms;
    s_cache_next = (uint8_t)((s_cache_next + 1u) %
                             APP_CONTROL_DEDUP_CACHE_CAPACITY);
}

static void prv_build_and_send_uncached(uint8_t request_opcode,
                                        uint8_t sequence,
                                        App_Control_Status_t status,
                                        uint8_t detail,
                                        uint32_t value)
{
    uint8_t response[APP_CONTROL_FRAME_LENGTH];
    prv_build_response(response, request_opcode, sequence, status,
                       detail, value);
    prv_send_response(response);
}

static bool prv_reserved_bytes_are_zero(const uint8_t* data)
{
    for (uint8_t i = 2u; i < APP_CONTROL_FRAME_LENGTH; i++) {
        if (data[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool prv_bytes_are_zero_from(const uint8_t* data, uint8_t first)
{
    for (uint8_t i = first; i < APP_CONTROL_FRAME_LENGTH; i++) {
        if (data[i] != 0u) {
            return false;
        }
    }
    return true;
}

static App_Control_Status_t prv_txn_status(
    App_Config_Transaction_Status_t status)
{
    switch (status) {
        case APP_CONFIG_TXN_OK:
            return APP_CONTROL_STATUS_OK;
        case APP_CONFIG_TXN_UNKNOWN_KEY:
            return APP_CONTROL_STATUS_UNKNOWN_SIGNAL_KEY;
        case APP_CONFIG_TXN_VALUE_OUT_OF_RANGE:
            return APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE;
        case APP_CONFIG_TXN_UNSUPPORTED_SIGNAL:
            return APP_CONTROL_STATUS_BAD_ARGUMENT;
        case APP_CONFIG_TXN_RESOURCE_FULL:
            return APP_CONTROL_STATUS_RESOURCE_FULL;
        case APP_CONFIG_TXN_COUNT_MISMATCH:
            return APP_CONTROL_STATUS_COUNT_MISMATCH;
        case APP_CONFIG_TXN_REJECTED:
            return APP_CONTROL_STATUS_TRANSACTION_REJECTED;
        case APP_CONFIG_TXN_INTERNAL_ERROR:
            return APP_CONTROL_STATUS_INTERNAL_ERROR;
        case APP_CONFIG_TXN_ALREADY_OPEN:
            return APP_CONTROL_STATUS_BUSY;
        case APP_CONFIG_TXN_BLOB_INCOMPLETE:
            return APP_CONTROL_STATUS_BLOB_INCOMPLETE;
        case APP_CONFIG_TXN_BAD_ARGUMENT:
            return APP_CONTROL_STATUS_BAD_ARGUMENT;
        case APP_CONFIG_TXN_UNKNOWN_PROFILE:
            return APP_CONTROL_STATUS_UNKNOWN_PROFILE;
        case APP_CONFIG_TXN_PROFILE_KEY_NOT_ALLOWED:
            return APP_CONTROL_STATUS_PROFILE_KEY_NOT_ALLOWED;
        case APP_CONFIG_TXN_PROFILE_MISMATCH:
            return APP_CONTROL_STATUS_PROFILE_MISMATCH;
        case APP_CONFIG_TXN_INVALID_STATE:
        case APP_CONFIG_TXN_NOT_OPEN:
        default:
            return APP_CONTROL_STATUS_INVALID_STATE;
    }
}

static void prv_handle_set_const(uint8_t sequence,
                                 const uint8_t* data,
                                 uint8_t* response)
{
    const uint16_t signal_key = prv_read_u16_le(&data[2]);
    const uint32_t requested_raw = prv_read_u32_le(&data[4]);
    uint32_t applied_raw = 0u;

    switch (App_Gen_Set_Constant_Raw(signal_key, requested_raw,
                                     &applied_raw)) {
        case APP_SET_CONST_OK:
            prv_build_response(response, APP_CONTROL_OPCODE_SET_CONST, sequence,
                               APP_CONTROL_STATUS_OK, 0u, applied_raw);
            break;
        case APP_SET_CONST_UNKNOWN_KEY:
            prv_build_response(response, APP_CONTROL_OPCODE_SET_CONST, sequence,
                               APP_CONTROL_STATUS_UNKNOWN_SIGNAL_KEY, 0u, 0u);
            break;
        case APP_SET_CONST_VALUE_OUT_OF_RANGE:
            prv_build_response(response, APP_CONTROL_OPCODE_SET_CONST, sequence,
                               APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE, 0u, 0u);
            break;
        case APP_SET_CONST_UNSUPPORTED_SIGNAL:
            prv_build_response(response, APP_CONTROL_OPCODE_SET_CONST, sequence,
                               APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
            break;
        case APP_SET_CONST_TRANSACTION_OPEN:
            prv_build_response(response, APP_CONTROL_OPCODE_SET_CONST, sequence,
                               APP_CONTROL_STATUS_INVALID_STATE, 0u, 0u);
            break;
        default:
            prv_build_response(response, APP_CONTROL_OPCODE_SET_CONST, sequence,
                               APP_CONTROL_STATUS_BUSY, 0u, 0u);
            break;
    }
}

static void prv_handle_start(uint8_t sequence,
                             const uint8_t* data,
                             uint8_t* response)
{
    if (!prv_reserved_bytes_are_zero(data)) {
        prv_build_response(response, APP_CONTROL_OPCODE_START, sequence,
                           APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }
    if (!App_Gen_Active_Profile_Can_Start()) {
        prv_build_response(response, APP_CONTROL_OPCODE_START, sequence,
                           APP_CONTROL_STATUS_INVALID_STATE, 0u, 0u);
        return;
    }

    App_Gen_Start(0u, J1939_SCHED_MODE_SAE);
    prv_build_response(response, APP_CONTROL_OPCODE_START, sequence,
                       APP_CONTROL_STATUS_OK, 0u, 0u);
}

static void prv_handle_stop(uint8_t sequence,
                            const uint8_t* data,
                            uint8_t* response)
{
    if (!prv_reserved_bytes_are_zero(data)) {
        prv_build_response(response, APP_CONTROL_OPCODE_STOP, sequence,
                           APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }

    /* STOP is deliberately idempotent so a retry is safe. */
    App_Gen_Stop();
    prv_build_response(response, APP_CONTROL_OPCODE_STOP, sequence,
                       APP_CONTROL_STATUS_OK, 0u, 0u);
}

static void prv_handle_clear_config(uint8_t sequence,
                                    const uint8_t* data,
                                    uint8_t* response)
{
    if (!prv_reserved_bytes_are_zero(data)) {
        prv_build_response(response, APP_CONTROL_OPCODE_CLEAR_CONFIG, sequence,
                           APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }
    if (App_Gen_Is_Running()) {
        prv_build_response(response, APP_CONTROL_OPCODE_CLEAR_CONFIG, sequence,
                           APP_CONTROL_STATUS_INVALID_STATE, 0u, 0u);
        return;
    }
    if (App_Gen_Config_Transaction_Open()) {
        prv_build_response(response, APP_CONTROL_OPCODE_CLEAR_CONFIG, sequence,
                           APP_CONTROL_STATUS_INVALID_STATE, 0u, 0u);
        return;
    }

    /* CLEAR_CONFIG is intentionally separate from STOP: STOP preserves the
     * current profile, while CLEAR_CONFIG releases every signal/PGN slot. */
    App_Gen_Clear();
    prv_build_response(response, APP_CONTROL_OPCODE_CLEAR_CONFIG, sequence,
                       APP_CONTROL_STATUS_OK, 0u, 0u);
}

static void prv_handle_get_status(uint8_t sequence,
                                  const uint8_t* data,
                                  uint8_t* response)
{
    const uint8_t page = data[2];
    if (!prv_bytes_are_zero_from(data, 3u) ||
        (page > APP_CONTROL_STATUS_PAGE_LAST)) {
        prv_build_response(response, APP_CONTROL_OPCODE_GET_STATUS, sequence,
                           APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }

    uint8_t runtime_flags = 0u;
    if (App_Gen_Is_Running()) {
        runtime_flags |= APP_CONTROL_RUNTIME_RUNNING;
    }
    if (App_Gen_Is_Signal_Configured(J1939_KEY_EEC1_SPN190)) {
        runtime_flags |= APP_CONTROL_RUNTIME_ENGINE_SPEED_CONFIGURED;
    }
    if (App_Gen_Config_Transaction_Open()) {
        runtime_flags |= APP_CONTROL_RUNTIME_TRANSACTION_OPEN;
    }
    if (App_Gen_Config_Transaction_Rejected()) {
        runtime_flags |= APP_CONTROL_RUNTIME_TRANSACTION_REJECTED;
    }
    if (App_Gen_Get_Config_Generation() != 0u) {
        runtime_flags |= APP_CONTROL_RUNTIME_COMMITTED;
    }

    uint32_t value = APP_CONTROL_PROTOCOL_INFO_VALUE;
    uint8_t detail = runtime_flags;
    switch (page) {
        case APP_CONTROL_STATUS_PAGE_RUNTIME:
            value = (uint32_t)App_Gen_Get_Config_Generation() |
                    ((uint32_t)App_Gen_Get_Active_Signal_Count() << 16) |
                    ((uint32_t)App_Gen_Get_Active_PGN_Count() << 24);
            break;
        case APP_CONTROL_STATUS_PAGE_CONFIG:
            value = App_Gen_Get_Config_Crc();
            break;
        case APP_CONTROL_STATUS_PAGE_LIMITS:
            detail = (uint8_t)APP_CONTROL_FEATURES;
            value = (uint32_t)J1939_PROFILE_SIGNAL_CAPACITY |
                    ((uint32_t)J1939_PROFILE_PGN_CAPACITY << 8) |
                    ((uint32_t)J1939_WAVEFORM_POOL_CAPACITY << 16) |
                    ((uint32_t)J1939_VALUE_STORE_BLOB_MAX_BYTES << 24);
            break;
        case APP_CONTROL_STATUS_PAGE_PROFILE: {
            const uint8_t profile_id = App_Gen_Get_Active_Profile_Id();
            const J1939_Customer_Profile_t* profile =
                J1939_Customer_Profile_Find(profile_id);
            if (profile != NULL) {
                detail = profile->flags;
                if (profile->unresolved_mode_count != 0u) {
                    detail |= J1939_PROFILE_FLAG_UNRESOLVED_MODE;
                }
                value = (uint32_t)profile->profile_id |
                        ((uint32_t)profile->signal_count << 8) |
                        ((uint32_t)profile->pgn_count << 16) |
                        ((uint32_t)profile->baseline_load_500k_decipct << 24);
            } else {
                detail = 0u;
                value = (uint32_t)J1939_PROFILE_ID_CUSTOM |
                        ((uint32_t)App_Gen_Get_Active_Signal_Count() << 8) |
                        ((uint32_t)App_Gen_Get_Active_PGN_Count() << 16) |
                        (0xFFu << 24);
            }
            break;
        }
        default:
            break;
    }
    prv_build_response(response, APP_CONTROL_OPCODE_GET_STATUS, sequence,
                       APP_CONTROL_STATUS_OK, detail, value);
}

_Static_assert((J1939_PROFILE_SIGNAL_CAPACITY <= 0xFFu) &&
               (J1939_PROFILE_PGN_CAPACITY <= 0xFFu) &&
               (J1939_WAVEFORM_POOL_CAPACITY <= 0xFFu) &&
               (J1939_VALUE_STORE_BLOB_MAX_BYTES <= 0xFFu),
               "GET_STATUS limits page packs each limit into one byte");

static void prv_handle_begin_config(uint8_t sequence,
                                    const uint8_t* data,
                                    uint8_t* response)
{
    const uint32_t catalog_crc = prv_read_u32_le(&data[2]);
    const uint16_t expected_count = data[6];
    const uint8_t profile_blob = data[7];
    const uint8_t expected_blob_count =
        profile_blob & APP_CONTROL_BEGIN_BLOB_MASK;
    const uint8_t selector =
        profile_blob >> APP_CONTROL_BEGIN_PROFILE_SHIFT;
    const uint8_t profile_id =
        (selector == APP_CONTROL_BEGIN_PROFILE_SELECTOR_CUSTOM)
            ? J1939_PROFILE_ID_CUSTOM : (uint8_t)(selector - 1u);
    if (catalog_crc != J1939_CATALOG_CRC32) {
        prv_build_response(response, APP_CONTROL_OPCODE_BEGIN_CONFIG, sequence,
                           APP_CONTROL_STATUS_CATALOG_MISMATCH, 0u,
                           J1939_CATALOG_CRC32);
        return;
    }

    const App_Control_Status_t status = prv_txn_status(
        App_Gen_Begin_Profile_Config(expected_count, expected_blob_count,
                                     profile_id));
    prv_build_response(response, APP_CONTROL_OPCODE_BEGIN_CONFIG, sequence,
                       status, (uint8_t)expected_count,
                       J1939_CATALOG_CRC32);
}

static void prv_handle_set_signal_raw(uint8_t sequence,
                                      const uint8_t* data,
                                      uint8_t* response)
{
    const uint16_t signal_key = prv_read_u16_le(&data[2]);
    const uint32_t raw_value = prv_read_u32_le(&data[4]);
    uint16_t staged_count = 0u;
    const App_Control_Status_t status = prv_txn_status(
        App_Gen_Stage_Constant_Raw(signal_key, raw_value, &staged_count));
    prv_build_response(response, APP_CONTROL_OPCODE_SET_SIGNAL_RAW, sequence,
                       status, (uint8_t)staged_count,
                       (status == APP_CONTROL_STATUS_OK) ? raw_value : 0u);
}

static void prv_handle_write_blob(uint8_t sequence,
                                  const uint8_t* data,
                                  uint8_t* response)
{
    const uint16_t signal_key = data[2];
    const uint8_t total_length = data[3];
    const uint8_t offset = data[4];
    uint16_t written = 0u;
    const App_Control_Status_t status = prv_txn_status(
        App_Gen_Stage_Blob_Chunk(signal_key, total_length, offset, &data[5],
                                 &written));
    prv_build_response(response, APP_CONTROL_OPCODE_WRITE_BLOB, sequence,
                       status, total_length, written);
}

static void prv_handle_set_global_percent(uint8_t sequence,
                                          const uint8_t* data,
                                          uint8_t* response)
{
    if (!prv_bytes_are_zero_from(data, 4u)) {
        prv_build_response(response, APP_CONTROL_OPCODE_SET_GLOBAL_PERCENT,
                           sequence, APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }

    uint16_t generation = 0u;
    uint16_t affected = 0u;
    uint16_t excluded = 0u;
    const App_Control_Status_t status = prv_txn_status(
        App_Gen_Set_Global_Percent(prv_read_u16_le(&data[2]), &generation,
                                   &affected, &excluded));
    const uint8_t excluded_u8 = (excluded > 0xFFu) ? 0xFFu : (uint8_t)excluded;
    prv_build_response(response, APP_CONTROL_OPCODE_SET_GLOBAL_PERCENT,
                       sequence, status, excluded_u8,
                       (uint32_t)generation | ((uint32_t)affected << 16));
}

static void prv_handle_commit_config(uint8_t sequence,
                                     const uint8_t* data,
                                     uint8_t* response)
{
    if (!prv_reserved_bytes_are_zero(data)) {
        prv_build_response(response, APP_CONTROL_OPCODE_COMMIT_CONFIG, sequence,
                           APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }

    uint16_t generation = 0u;
    uint16_t signal_count = 0u;
    uint8_t pgn_count = 0u;
    const App_Control_Status_t status = prv_txn_status(
        App_Gen_Commit_Config(&generation, &signal_count, &pgn_count));
    const uint32_t summary = (uint32_t)generation |
                             ((uint32_t)signal_count << 16);
    prv_build_response(response, APP_CONTROL_OPCODE_COMMIT_CONFIG, sequence,
                       status, pgn_count,
                       (status == APP_CONTROL_STATUS_OK) ? summary : 0u);
}

static void prv_handle_abort_config(uint8_t sequence,
                                    const uint8_t* data,
                                    uint8_t* response)
{
    if (!prv_reserved_bytes_are_zero(data)) {
        prv_build_response(response, APP_CONTROL_OPCODE_ABORT_CONFIG, sequence,
                           APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }
    App_Gen_Abort_Config();
    prv_build_response(response, APP_CONTROL_OPCODE_ABORT_CONFIG, sequence,
                       APP_CONTROL_STATUS_OK, 0u, 0u);
}

static void prv_handle_control_frame(const J1939_Frame_Header_t* header,
                                     const uint8_t* data,
                                     uint8_t data_length)
{
    if ((header == NULL) || (data == NULL)) {
        return;
    }

    s_stats.received++;
    if ((header->pgn != J1939_PGN_PROPRIETARY_A_CONTROL) ||
        (header->priority != APP_CONTROL_PRIORITY) ||
        (header->source_address != APP_CONTROL_TESTER_ADDRESS) ||
        (header->destination_address != APP_CONTROL_STM32_ADDRESS)) {
        s_stats.ignored_endpoint++;
        return;
    }

    if ((data_length > 0u) &&
        ((data[0] & APP_CONTROL_RESPONSE_MASK) != 0u)) {
        s_stats.ignored_response++;
        return;
    }

    /* Without both opcode and sequence no correlated response can be built. */
    if (data_length < 2u) {
        s_stats.malformed_without_sequence++;
        return;
    }

    const uint8_t opcode = data[0];
    const uint8_t sequence = data[1];
    if (data_length != APP_CONTROL_FRAME_LENGTH) {
        prv_build_and_send_uncached(opcode, sequence,
                                    APP_CONTROL_STATUS_BAD_DLC,
                                    data_length, 0u);
        return;
    }

    if (sequence == 0u) {
        prv_build_and_send_uncached(opcode, sequence,
                                    APP_CONTROL_STATUS_BAD_ARGUMENT,
                                    0u, 0u);
        return;
    }

    const uint32_t now_ms = hal_time_ms();
    uint8_t response[APP_CONTROL_FRAME_LENGTH];
    switch (prv_cache_lookup(header->source_address, data, now_ms, response)) {
        case APP_CONTROL_CACHE_REPLAY:
            prv_send_response(response);
            return;
        case APP_CONTROL_CACHE_CONFLICT:
            prv_build_and_send_uncached(
                opcode, sequence, APP_CONTROL_STATUS_DUPLICATE_CONFLICT,
                0u, 0u);
            return;
        default:
            break;
    }

    switch (opcode) {
        case APP_CONTROL_OPCODE_TC1001:
            if (!prv_reserved_bytes_are_zero(data)) {
                prv_build_response(response, opcode, sequence,
                                   APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
                break;
            }
            prv_build_response(response, opcode, sequence,
                               APP_CONTROL_STATUS_OK, 0u, 0u);
            break;
        case APP_CONTROL_OPCODE_STOP:
            prv_handle_stop(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_CLEAR_CONFIG:
            prv_handle_clear_config(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_SET_CONST:
            prv_handle_set_const(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_BEGIN_CONFIG:
            prv_handle_begin_config(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_SET_SIGNAL_RAW:
            prv_handle_set_signal_raw(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_COMMIT_CONFIG:
            prv_handle_commit_config(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_ABORT_CONFIG:
            prv_handle_abort_config(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_WRITE_BLOB:
            prv_handle_write_blob(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_SET_GLOBAL_PERCENT:
            prv_handle_set_global_percent(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_START:
            prv_handle_start(sequence, data, response);
            break;
        case APP_CONTROL_OPCODE_GET_STATUS:
            prv_handle_get_status(sequence, data, response);
            break;
        default:
            prv_build_response(response, opcode, sequence,
                               APP_CONTROL_STATUS_UNSUPPORTED_OPCODE, 0u, 0u);
            break;
    }

    prv_cache_store(header->source_address, data, response, now_ms);
    prv_send_response(response);
}

void App_Control_Can_Init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_cache, 0, sizeof(s_cache));
    memset(s_tx_queue, 0, sizeof(s_tx_queue));
    s_cache_next = 0u;
    s_tx_queue_head = 0u;
    s_tx_queue_count = 0u;
    s_tx_retry_not_before_ms = 0u;
    J1939_Rx_Router_Set_Control_Handler(prv_handle_control_frame);
}

void App_Control_Can_Process(void)
{
    const uint32_t now_ms = hal_time_ms();

    while (s_tx_queue_count > 0u) {
        App_Control_Tx_Entry_t* entry = &s_tx_queue[s_tx_queue_head];
        if ((now_ms - entry->queued_ms) > APP_CONTROL_DEDUP_WINDOW_MS) {
            s_stats.responses_dropped++;
            prv_pop_tx_queue();
            continue;
        }
        if (!prv_time_reached(now_ms, s_tx_retry_not_before_ms)) {
            return;
        }

        const J1939_Link_Status_t link_status =
            prv_transmit_response(entry->response);
        if (link_status == J1939_LINK_OK) {
            s_stats.responses_queued++;
            prv_pop_tx_queue();
            continue;
        }
        if (link_status == J1939_LINK_BUSY) {
            s_stats.responses_busy++;
        } else {
            s_stats.responses_failed++;
        }
        s_tx_retry_not_before_ms = now_ms + APP_CONTROL_TX_RETRY_DELAY_MS;
        return;
    }
}

void App_Control_Can_Get_Stats(App_Control_Can_Stats_t* stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}
