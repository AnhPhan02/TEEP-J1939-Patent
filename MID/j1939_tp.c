/*
 * =============================================================================
 * FILE : j1939_tp.c
 * WHAT : J1939-21 BAM transmitter (see j1939_tp.h).
 * =============================================================================
 */

#include "j1939_tp.h"
#include "j1939_link.h"
#include <stddef.h>
#include <string.h>

typedef enum {
    TP_STATE_IDLE = 0,
    TP_STATE_PREPARED,      /* buffer reserved, not started */
    TP_STATE_SENDING
} Tp_State_t;

typedef struct {
    uint8_t  payload[J1939_TP_MAX_PAYLOAD_BYTES];
    uint32_t pgn;
    uint32_t next_due_ms;   /* next send attempt */
    uint32_t slot_ms;       /* when the current packet was due */
    uint16_t length;
    uint8_t  packets;
    uint8_t  next_packet;   /* 0 = TP.CM, 1..packets = TP.DT sequence */
    uint8_t  source_address;
    uint8_t  state;
} Tp_Session_t;

static Tp_Session_t     s_session;
static J1939_Tp_Stats_t s_stats;

static bool prv_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void prv_build_packet(uint8_t data[8])
{
    if (s_session.next_packet == 0u) {
        data[0] = J1939_TP_CM_BAM;
        data[1] = (uint8_t)s_session.length;
        data[2] = (uint8_t)(s_session.length >> 8);
        data[3] = s_session.packets;
        data[4] = 0xFFu;
        data[5] = (uint8_t)s_session.pgn;
        data[6] = (uint8_t)(s_session.pgn >> 8);
        data[7] = (uint8_t)(s_session.pgn >> 16);
        return;
    }
    /* Bytes past the end of the message are 0xFF (J1939-21 5.10.3.5). */
    memset(data, 0xFF, 8u);
    data[0] = s_session.next_packet;
    const uint16_t offset =
        (uint16_t)((s_session.next_packet - 1u) * J1939_TP_DT_DATA_BYTES);
    const uint16_t remaining = (uint16_t)(s_session.length - offset);
    const uint16_t count = (remaining < J1939_TP_DT_DATA_BYTES)
        ? remaining : J1939_TP_DT_DATA_BYTES;
    memcpy(&data[1], &s_session.payload[offset], count);
}

void J1939_Tp_Init(void)
{
    memset(&s_session, 0, sizeof(s_session));
    memset(&s_stats, 0, sizeof(s_stats));
}

void J1939_Tp_Cancel(void)
{
    if (s_session.state == TP_STATE_SENDING) {
        s_stats.aborted++;
    }
    s_session.state = TP_STATE_IDLE;
}

bool J1939_Tp_Is_Busy(void)
{
    return s_session.state == TP_STATE_SENDING;
}

uint8_t* J1939_Tp_Bam_Prepare(uint16_t length)
{
    if ((s_session.state == TP_STATE_SENDING) || (length <= 8u) ||
        (length > J1939_TP_MAX_PAYLOAD_BYTES)) {
        return NULL;
    }
    memset(s_session.payload, 0xFF, sizeof(s_session.payload));
    s_session.length = length;
    s_session.state = TP_STATE_PREPARED;
    return s_session.payload;
}

bool J1939_Tp_Bam_Start(uint32_t pgn, uint8_t source_address, uint32_t now_ms)
{
    if (s_session.state != TP_STATE_PREPARED) {
        return false;
    }
    s_session.pgn = pgn;
    s_session.source_address = source_address;
    s_session.packets = (uint8_t)((s_session.length + J1939_TP_DT_DATA_BYTES - 1u) /
                                  J1939_TP_DT_DATA_BYTES);
    s_session.next_packet = 0u;
    s_session.slot_ms = now_ms;
    s_session.next_due_ms = now_ms;
    s_session.state = TP_STATE_SENDING;
    s_stats.started++;
    return true;
}

void J1939_Tp_Process(uint32_t now_ms)
{
    if ((s_session.state != TP_STATE_SENDING) ||
        !prv_time_reached(now_ms, s_session.next_due_ms)) {
        return;
    }
    if (prv_time_reached(now_ms, s_session.slot_ms + J1939_TP_BAM_ABORT_LATE_MS)) {
        J1939_Tp_Cancel();
        return;
    }

    uint8_t data[8];
    prv_build_packet(data);
    /* J1939_Link_Send addresses the global destination: PDU1 TP.CM/TP.DT
     * with DA 0xFF is exactly the BAM identifier. */
    const uint32_t pgn = (s_session.next_packet == 0u) ? J1939_PGN_TP_CM
                                                       : J1939_PGN_TP_DT;
    if (J1939_Link_Send(pgn, data, 8u, J1939_TP_PRIORITY,
                        s_session.source_address) != J1939_LINK_OK) {
        s_stats.busy++;
        s_session.next_due_ms = now_ms + 1u;
        return;
    }

    s_stats.frames_sent++;
    if (s_session.next_packet >= s_session.packets) {
        s_session.state = TP_STATE_IDLE;
        s_stats.completed++;
        return;
    }
    s_session.next_packet++;
    s_session.slot_ms = now_ms + J1939_TP_BAM_PACKET_GAP_MS;
    s_session.next_due_ms = s_session.slot_ms;
}

void J1939_Tp_Get_Stats(J1939_Tp_Stats_t* out)
{
    if (out != NULL) {
        *out = s_stats;
    }
}
