/*
 * =============================================================================
 * FILE : j1939_rx_router.c
 * WHAT : Single-owner foreground dispatcher for received J1939 frames.
 * =============================================================================
 */

#include "j1939_rx_router.h"
#include "j1939_link.h"
#include <stddef.h>
#include <string.h>

static J1939_Rx_Control_Handler_t s_control_handler = NULL;
static J1939_Rx_Router_Stats_t s_stats;

void J1939_Rx_Router_Init(void)
{
    s_control_handler = NULL;
    memset(&s_stats, 0, sizeof(s_stats));
}

void J1939_Rx_Router_Set_Control_Handler(J1939_Rx_Control_Handler_t handler)
{
    s_control_handler = handler;
}

void J1939_Rx_Router_Poll(uint8_t frame_budget)
{
    for (uint8_t i = 0u; i < frame_budget; i++) {
        J1939_Frame_Header_t header;
        uint8_t data[8] = {0u};
        uint8_t data_length = 0u;

        if (!J1939_Link_Receive(&header, data, &data_length)) {
            break;
        }

        s_stats.received++;
        if ((header.pgn == J1939_PGN_PROPRIETARY_A_CONTROL) &&
            (s_control_handler != NULL)) {
            s_stats.control_routed++;
            s_control_handler(&header, data, data_length);
        } else {
            s_stats.unhandled++;
        }
    }
}

void J1939_Rx_Router_Get_Stats(J1939_Rx_Router_Stats_t* stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}
