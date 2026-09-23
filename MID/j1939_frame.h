/*
 * =============================================================================
 * FILE : j1939_frame.h
 * WHAT : SAE J1939-21 29-bit identifier <-> PGN / priority / address mapping.
 *        Pure logic, no hardware access.
 * =============================================================================
 */

#ifndef J1939_FRAME_H
#define J1939_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define J1939_ADDR_GLOBAL       0xFFu   /* Broadcast destination address */
#define J1939_PF_PDU2_MIN       240u    /* PF >= 240 -> PDU2 (broadcast, PS = group ext) */

typedef struct {
    uint32_t pgn;                   /* 18-bit Parameter Group Number */
    uint8_t  priority;              /* 0 (highest) .. 7 */
    uint8_t  source_address;
    uint8_t  destination_address;   /* PDU1 only; J1939_ADDR_GLOBAL for PDU2 */
} J1939_Frame_Header_t;

/* Build 29-bit CAN ID. For PDU1 the PS field carries destination_address. */
uint32_t J1939_Frame_Build_Id(const J1939_Frame_Header_t* hdr);

/* Split 29-bit CAN ID into header fields. PDU1 PGNs have PS masked to 0. */
void J1939_Frame_Parse_Id(uint32_t can_id, J1939_Frame_Header_t* hdr);

#ifdef __cplusplus
}
#endif
#endif /* J1939_FRAME_H */
