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
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define APP_CONTROL_DEDUP_CACHE_CAPACITY  16u
#define APP_CONTROL_DEDUP_WINDOW_MS       2000u

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

static App_Control_Can_Stats_t s_stats;
static App_Control_Cache_Entry_t s_cache[APP_CONTROL_DEDUP_CACHE_CAPACITY];
static uint8_t s_cache_next;

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

static void prv_send_response(const uint8_t* response)
{
    J1939_Link_Status_t link_status = J1939_Link_Send_To(
        J1939_PGN_PROPRIETARY_A_CONTROL,
        response,
        APP_CONTROL_FRAME_LENGTH,
        APP_CONTROL_PRIORITY,
        APP_CONTROL_STM32_ADDRESS,
        APP_CONTROL_TESTER_ADDRESS);

    if (link_status == J1939_LINK_OK) {
        s_stats.responses_queued++;
    } else if (link_status == J1939_LINK_BUSY) {
        s_stats.responses_busy++;
    } else {
        s_stats.responses_failed++;
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

static void prv_handle_set_const(uint8_t sequence,
                                 const uint8_t* data,
                                 uint8_t* response)
{
    const J1939_Signal_Key_t signal_key =
        (J1939_Signal_Key_t)prv_read_u16_le(&data[2]);
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
    if (!App_Gen_Has_Configured_Signals()) {
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

static void prv_handle_get_status(uint8_t sequence,
                                  const uint8_t* data,
                                  uint8_t* response)
{
    if (!prv_reserved_bytes_are_zero(data)) {
        prv_build_response(response, APP_CONTROL_OPCODE_GET_STATUS, sequence,
                           APP_CONTROL_STATUS_BAD_ARGUMENT, 0u, 0u);
        return;
    }

    uint8_t runtime_flags = 0u;
    if (App_Gen_Is_Running()) {
        runtime_flags |= APP_CONTROL_RUNTIME_RUNNING;
    }
    if (App_Gen_Is_Signal_Configured(J1939_SIGNAL_KEY_ENGINE_SPEED)) {
        runtime_flags |= APP_CONTROL_RUNTIME_ENGINE_SPEED_CONFIGURED;
    }
    prv_build_response(response, APP_CONTROL_OPCODE_GET_STATUS, sequence,
                       APP_CONTROL_STATUS_OK, runtime_flags,
                       APP_CONTROL_PROTOCOL_INFO_VALUE);
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
        case APP_CONTROL_OPCODE_SET_CONST:
            prv_handle_set_const(sequence, data, response);
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
    s_cache_next = 0u;
    J1939_Rx_Router_Set_Control_Handler(prv_handle_control_frame);
}

void App_Control_Can_Get_Stats(App_Control_Can_Stats_t* stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}
