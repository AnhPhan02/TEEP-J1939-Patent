/*
 * =============================================================================
 * FILE : j1939_link.c
 * WHAT : J1939 data-link send/receive on top of the HAL CAN driver.
 * =============================================================================
 */

#include "j1939_link.h"
#include <stddef.h>

bool J1939_Link_Init(uint32_t baud_kbps)
{
    return hal_can_init(baud_kbps * 1000u);
}

J1939_Link_Status_t J1939_Link_Send(uint32_t pgn,
                                    const uint8_t* data,
                                    uint8_t data_length,
                                    uint8_t priority,
                                    uint8_t source_address)
{
    J1939_Frame_Header_t hdr;
    hdr.pgn = pgn;
    hdr.priority = priority;
    hdr.source_address = source_address;
    hdr.destination_address = J1939_ADDR_GLOBAL;

    switch (hal_can_send_ext(J1939_Frame_Build_Id(&hdr), data, data_length)) {
        case HCAN_TX_OK:      return J1939_LINK_OK;
        case HCAN_TX_TIMEOUT: return J1939_LINK_BUSY;
        default:              return J1939_LINK_ERROR;
    }
}

bool J1939_Link_Receive(J1939_Frame_Header_t* hdr, uint8_t* data, uint8_t* data_length)
{
    if (hdr == NULL || data == NULL || data_length == NULL) {
        return false;
    }

    uint32_t can_id;
    if (!hal_can_receive_ext(&can_id, data, data_length)) {
        return false;
    }

    J1939_Frame_Parse_Id(can_id, hdr);
    return true;
}

void J1939_Link_Get_Bit_Timing(J1939_Link_Bit_Timing_t* out)
{
    hal_can_get_bit_timing(out);
}

void J1939_Link_Get_Bus_Error(J1939_Link_Bus_Error_t* out)
{
    hal_can_get_error(out);
}
