/*
 * =============================================================================
 * FILE : j1939_link.h
 * WHAT : J1939 data-link send/receive on top of the HAL CAN driver.
 *        The only MID module that talks to hal_can.
 * =============================================================================
 */

#ifndef J1939_LINK_H
#define J1939_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_frame.h"
#include "hal_can.h"

typedef enum {
    J1939_LINK_OK    = 0,
    J1939_LINK_BUSY  = 1,   /* Controller mailboxes full (bus not ACKing / overloaded) */
    J1939_LINK_ERROR = 2
} J1939_Link_Status_t;

typedef hal_can_bit_timing_t J1939_Link_Bit_Timing_t;
typedef hal_can_error_t      J1939_Link_Bus_Error_t;

/* Initialize the underlying CAN controller (250 or 500 kbps for J1939) */
bool J1939_Link_Init(uint32_t baud_kbps);

/* Broadcast one PGN (destination = global) */
J1939_Link_Status_t J1939_Link_Send(uint32_t pgn,
                                    const uint8_t* data,
                                    uint8_t data_length,
                                    uint8_t priority,
                                    uint8_t source_address);

/* Poll for one received J1939 frame */
bool J1939_Link_Receive(J1939_Frame_Header_t* hdr, uint8_t* data, uint8_t* data_length);

void J1939_Link_Get_Bit_Timing(J1939_Link_Bit_Timing_t* out);
void J1939_Link_Get_Bus_Error(J1939_Link_Bus_Error_t* out);

#ifdef __cplusplus
}
#endif
#endif /* J1939_LINK_H */
