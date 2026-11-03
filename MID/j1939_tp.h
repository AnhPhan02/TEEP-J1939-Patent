/*
 * =============================================================================
 * FILE : j1939_tp.h
 * WHAT : J1939-21 transport protocol, transmit side, BAM only (broadcast of
 *        9..J1939_TP_MAX_PAYLOAD_BYTES byte PGNs to the global address).
 *
 *   J1939_Tp_Bam_Prepare(len) -> fill the returned buffer -> J1939_Tp_Bam_Start
 *   J1939_Tp_Process(now) every loop:
 *        TP.CM_BAM, then TP.DT 1..n, J1939_TP_BAM_PACKET_GAP_MS apart
 *
 *        One session at a time. The payload is copied into the session at
 *        prepare time, so later value changes cannot alter a running BAM.
 *        A packet that cannot reach a mailbox for J1939_TP_BAM_ABORT_LATE_MS
 *        aborts the session (receivers would already have timed out).
 * =============================================================================
 */

#ifndef J1939_TP_H
#define J1939_TP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "j1939_signal_definitions.h"

#define J1939_PGN_TP_CM               0xEC00u
#define J1939_PGN_TP_DT               0xEB00u
#define J1939_TP_CM_BAM               32u
#define J1939_TP_PRIORITY             7u
#define J1939_TP_DT_DATA_BYTES        7u
#define J1939_TP_MAX_PAYLOAD_BYTES    J1939_CATALOG_MAX_PAYLOAD_BYTES
#define J1939_TP_BAM_PACKET_GAP_MS    50u     /* J1939-21: 50..200 ms */
#define J1939_TP_BAM_ABORT_LATE_MS    750u    /* receiver T1 */

typedef struct {
    uint32_t started;
    uint32_t completed;
    uint32_t aborted;       /* late packet, cancel, STOP or baud change */
    uint32_t frames_sent;   /* TP.CM + TP.DT handed to a mailbox */
    uint32_t busy;          /* send attempts that found no free mailbox */
} J1939_Tp_Stats_t;

/* Drop any session and clear statistics. */
void J1939_Tp_Init(void);

/* Drop a running session (counted as aborted); statistics are kept. */
void J1939_Tp_Cancel(void);

bool J1939_Tp_Is_Busy(void);

/* Reserve the session buffer for a payload of length bytes, pre-filled with
 * 0xFF. Returns NULL while a session runs or for a length outside
 * 9..J1939_TP_MAX_PAYLOAD_BYTES. */
uint8_t* J1939_Tp_Bam_Prepare(uint16_t length);

/* Start broadcasting the prepared payload; TP.CM goes out on the next
 * J1939_Tp_Process call at or after now_ms. */
bool J1939_Tp_Bam_Start(uint32_t pgn, uint8_t source_address, uint32_t now_ms);

void J1939_Tp_Process(uint32_t now_ms);

void J1939_Tp_Get_Stats(J1939_Tp_Stats_t* out);

#ifdef __cplusplus
}
#endif
#endif /* J1939_TP_H */
