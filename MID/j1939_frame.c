/*
 * =============================================================================
 * FILE : j1939_frame.c
 * WHAT : SAE J1939-21 29-bit identifier build / parse.
 *
 *   28..26    25   24   23..16   15..8   7..0
 *   Priority  EDP  DP   PF       PS      SA
 * =============================================================================
 */

#include "j1939_frame.h"
#include <stddef.h>

uint32_t J1939_Frame_Build_Id(const J1939_Frame_Header_t* hdr)
{
    if (hdr == NULL) {
        return 0u;
    }

    uint8_t pf = (uint8_t)((hdr->pgn >> 8) & 0xFFu);
    uint8_t ps = (uint8_t)(hdr->pgn & 0xFFu);
    uint8_t dp = (uint8_t)((hdr->pgn >> 16) & 0x01u);

    uint32_t can_id = ((uint32_t)(hdr->priority & 0x07u) << 26) |
                      ((uint32_t)dp << 24) |
                      ((uint32_t)pf << 16);

    if (pf < J1939_PF_PDU2_MIN) {
        /* PDU1 (destination specific): PS = destination address */
        can_id |= ((uint32_t)hdr->destination_address << 8);
    } else {
        /* PDU2 (broadcast): PS = group extension */
        can_id |= ((uint32_t)ps << 8);
    }

    can_id |= hdr->source_address;
    return can_id;
}

void J1939_Frame_Parse_Id(uint32_t can_id, J1939_Frame_Header_t* hdr)
{
    if (hdr == NULL) {
        return;
    }

    uint8_t pf = (uint8_t)((can_id >> 16) & 0xFFu);
    uint8_t ps = (uint8_t)((can_id >> 8) & 0xFFu);
    uint8_t dp = (uint8_t)((can_id >> 24) & 0x01u);

    hdr->priority = (uint8_t)((can_id >> 26) & 0x07u);
    hdr->source_address = (uint8_t)(can_id & 0xFFu);

    if (pf < J1939_PF_PDU2_MIN) {
        hdr->pgn = ((uint32_t)dp << 16) | ((uint32_t)pf << 8);
        hdr->destination_address = ps;
    } else {
        hdr->pgn = ((uint32_t)dp << 16) | ((uint32_t)pf << 8) | ps;
        hdr->destination_address = J1939_ADDR_GLOBAL;
    }
}
