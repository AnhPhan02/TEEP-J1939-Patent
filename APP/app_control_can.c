/*
 * =============================================================================
 * FILE : app_control_can.c
 * WHAT : Decode UI control requests and return application-level responses.
 * =============================================================================
 */

#include "app_control_can.h"
#include "j1939_link.h"
#include "j1939_rx_router.h"
#include <stddef.h>
#include <string.h>

static App_Control_Can_Stats_t s_stats;

static void prv_write_u32_le(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void prv_send_response(uint8_t request_opcode,
                              uint8_t sequence,
                              App_Control_Status_t status,
                              uint8_t detail,
                              uint32_t value)
{
    uint8_t response[APP_CONTROL_FRAME_LENGTH] = {
        (uint8_t)(request_opcode | APP_CONTROL_RESPONSE_MASK),
        sequence,
        (uint8_t)status,
        detail,
        0u, 0u, 0u, 0u
    };
    prv_write_u32_le(&response[4], value);

    J1939_Link_Status_t link_status = J1939_Link_Send_To(
        J1939_PGN_PROPRIETARY_A_CONTROL,
        response,
        sizeof(response),
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
        prv_send_response(opcode, sequence, APP_CONTROL_STATUS_BAD_DLC,
                          data_length, 0u);
        return;
    }

    if (sequence == 0u) {
        prv_send_response(opcode, sequence, APP_CONTROL_STATUS_BAD_ARGUMENT,
                          0u, 0u);
        return;
    }

    if (opcode != APP_CONTROL_OPCODE_TC1001) {
        prv_send_response(opcode, sequence,
                          APP_CONTROL_STATUS_UNSUPPORTED_OPCODE, 0u, 0u);
        return;
    }

    /* TC1001 has no arguments. Bytes 2..7 are reserved and must be zero. */
    for (uint8_t i = 2u; i < APP_CONTROL_FRAME_LENGTH; i++) {
        if (data[i] != 0u) {
            prv_send_response(opcode, sequence,
                              APP_CONTROL_STATUS_BAD_ARGUMENT, i, 0u);
            return;
        }
    }

    prv_send_response(opcode, sequence, APP_CONTROL_STATUS_OK, 0u, 0u);
}

void App_Control_Can_Init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    J1939_Rx_Router_Set_Control_Handler(prv_handle_control_frame);
}

void App_Control_Can_Get_Stats(App_Control_Can_Stats_t* stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}
