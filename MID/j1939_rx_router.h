/*
 * =============================================================================
 * FILE : j1939_rx_router.h
 * WHAT : Single-owner foreground dispatcher for received J1939 frames.
 * =============================================================================
 */

#ifndef J1939_RX_ROUTER_H
#define J1939_RX_ROUTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "j1939_frame.h"

#define J1939_PGN_PROPRIETARY_A_CONTROL  61184u  /* 0xEF00, PDU1 */

typedef void (*J1939_Rx_Control_Handler_t)(const J1939_Frame_Header_t* header,
                                           const uint8_t* data,
                                           uint8_t data_length);

typedef struct {
    uint32_t received;
    uint32_t control_routed;
    uint32_t unhandled;
} J1939_Rx_Router_Stats_t;

void J1939_Rx_Router_Init(void);
void J1939_Rx_Router_Set_Control_Handler(J1939_Rx_Control_Handler_t handler);

/* Drain at most frame_budget frames. A finite budget keeps the superloop
 * responsive if the bus is continuously busy. */
void J1939_Rx_Router_Poll(uint8_t frame_budget);

void J1939_Rx_Router_Get_Stats(J1939_Rx_Router_Stats_t* stats);

#ifdef __cplusplus
}
#endif
#endif /* J1939_RX_ROUTER_H */
