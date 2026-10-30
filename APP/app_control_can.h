/*
 * =============================================================================
 * FILE : app_control_can.h
 * WHAT : Proprietary single-frame CAN control protocol used by the Python UI.
 * =============================================================================
 */

#ifndef APP_CONTROL_CAN_H
#define APP_CONTROL_CAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_CONTROL_PRIORITY             6u
#define APP_CONTROL_TESTER_ADDRESS       0xF9u
#define APP_CONTROL_STM32_ADDRESS        0x80u
#define APP_CONTROL_FRAME_LENGTH         8u
#define APP_CONTROL_RESPONSE_MASK        0x80u
#define APP_CONTROL_OPCODE_TC1001        0x01u
#define APP_CONTROL_OPCODE_STOP          0x02u
#define APP_CONTROL_OPCODE_START         0x03u
#define APP_CONTROL_OPCODE_GET_STATUS    0x04u
#define APP_CONTROL_OPCODE_CLEAR_CONFIG  0x05u
#define APP_CONTROL_OPCODE_SET_CONST     0x10u
#define APP_CONTROL_OPCODE_BEGIN_CONFIG  0x11u
#define APP_CONTROL_OPCODE_SET_SIGNAL_RAW 0x12u
#define APP_CONTROL_OPCODE_COMMIT_CONFIG 0x13u
#define APP_CONTROL_OPCODE_ABORT_CONFIG  0x14u
#define APP_CONTROL_OPCODE_WRITE_BLOB    0x15u
#define APP_CONTROL_OPCODE_SET_GLOBAL_PERCENT 0x16u

/* Request layouts (all little-endian, all frames exactly 8 bytes):
 * BEGIN_CONFIG  [op, seq, catalog_crc32, expected_scalar_u8, expected_blob_u8]
 *               (expected_blob_u8 was flags=0 in 2.0, so 2.0 clients still work)
 * SET_SIGNAL_RAW[op, seq, signal_key_u16, raw_value_u32]
 * WRITE_BLOB    [op, seq, signal_key_u8, total_length_u8, offset_u8, data[3]]
 *               payload bytes = min(3, total_length - offset); unused data
 *               bytes must be zero. The first chunk fixes total_length.
 * SET_GLOBAL_PERCENT [op, seq, percentage_bp_u16 (0..10000), reserved_zero[4]]
 * COMMIT/ABORT  [op, seq, reserved_zero[6]]
 * GET_STATUS    [op, seq, page, reserved_zero[5]]
 *
 * Responses [op|0x80, seq, status, detail, value_u32]:
 * COMMIT        detail=active PGN count, value[15:0]=generation,
 *               value[31:16]=active signal count (scalars + blobs)
 * WRITE_BLOB    detail=total_length, value=distinct blob bytes staged so far
 * SET_GLOBAL_PERCENT detail=excluded active-record count,
 *               value[15:0]=generation, value[31:16]=affected record count
 * GET_STATUS page 0 protocol: value = major, minor, schema, capability mask
 *            page 1 runtime:  value[15:0] generation, [23:16] signal count,
 *                             [31:24] PGN count
 *            page 2 config:   value = active configuration CRC32
 *                             (algorithm in j1939_value_store.h)
 *            page 3 limits:   value = max signals u8, max PGNs u8, waveform
 *                             pool u8, max blob bytes u8; detail = features
 * Every page carries the APP_CONTROL_RUNTIME_* flags in detail except page 3. */

#define APP_CONTROL_PROTOCOL_MAJOR       2u
#define APP_CONTROL_PROTOCOL_MINOR       1u
#define APP_CONTROL_SCHEMA_VERSION       1u

#define APP_CONTROL_CAPABILITY_STOP          (1u << 0)
#define APP_CONTROL_CAPABILITY_GET_STATUS    (1u << 1)
#define APP_CONTROL_CAPABILITY_DEDUP_CACHE   (1u << 2)
#define APP_CONTROL_CAPABILITY_SET_CONST     (1u << 3)
#define APP_CONTROL_CAPABILITY_START         (1u << 4)
#define APP_CONTROL_CAPABILITY_CLEAR_CONFIG  (1u << 5)
#define APP_CONTROL_CAPABILITY_CONFIG_TXN    (1u << 6)
#define APP_CONTROL_CAPABILITY_RUNTIME_INFO  (1u << 7)
#define APP_CONTROL_CAPABILITIES             \
    (APP_CONTROL_CAPABILITY_STOP |           \
     APP_CONTROL_CAPABILITY_GET_STATUS |     \
     APP_CONTROL_CAPABILITY_DEDUP_CACHE |    \
     APP_CONTROL_CAPABILITY_SET_CONST |      \
     APP_CONTROL_CAPABILITY_START |          \
     APP_CONTROL_CAPABILITY_CLEAR_CONFIG |   \
     APP_CONTROL_CAPABILITY_CONFIG_TXN |     \
     APP_CONTROL_CAPABILITY_RUNTIME_INFO)

#define APP_CONTROL_RUNTIME_RUNNING                  (1u << 0)
#define APP_CONTROL_RUNTIME_ENGINE_SPEED_CONFIGURED  (1u << 1)
#define APP_CONTROL_RUNTIME_TRANSACTION_OPEN         (1u << 2)
#define APP_CONTROL_RUNTIME_TRANSACTION_REJECTED     (1u << 3)
#define APP_CONTROL_RUNTIME_COMMITTED                 (1u << 4)

#define APP_CONTROL_STATUS_PAGE_PROTOCOL              0u
#define APP_CONTROL_STATUS_PAGE_RUNTIME               1u
#define APP_CONTROL_STATUS_PAGE_CONFIG                2u
#define APP_CONTROL_STATUS_PAGE_LIMITS                3u
#define APP_CONTROL_STATUS_PAGE_LAST                  APP_CONTROL_STATUS_PAGE_LIMITS

/* GET_STATUS page 3 detail: protocol features beyond the page-0 mask. A clear
 * bit means the firmware does not provide the feature yet. */
#define APP_CONTROL_FEATURE_WRITE_BLOB        (1u << 0)
#define APP_CONTROL_FEATURE_GLOBAL_PERCENT    (1u << 1)
#define APP_CONTROL_FEATURE_CONFIG_CRC        (1u << 2)
#define APP_CONTROL_FEATURE_SIGNAL_MODE       (1u << 3)
#define APP_CONTROL_FEATURE_TRANSPORT_TP      (1u << 4)
#define APP_CONTROL_FEATURE_PERSISTENCE       (1u << 5)
#define APP_CONTROL_FEATURE_REQUEST_PGN       (1u << 6)
#define APP_CONTROL_FEATURES                  \
    (APP_CONTROL_FEATURE_WRITE_BLOB |         \
     APP_CONTROL_FEATURE_GLOBAL_PERCENT |     \
     APP_CONTROL_FEATURE_CONFIG_CRC)

/* GET_STATUS response: detail = APP_CONTROL_RUNTIME_* flags. The LE value32
 * bytes are protocol major, protocol minor, schema and capability mask. */
#define APP_CONTROL_PROTOCOL_INFO_VALUE                         \
    ((uint32_t)APP_CONTROL_PROTOCOL_MAJOR |                     \
     ((uint32_t)APP_CONTROL_PROTOCOL_MINOR << 8) |              \
     ((uint32_t)APP_CONTROL_SCHEMA_VERSION << 16) |             \
     ((uint32_t)APP_CONTROL_CAPABILITIES << 24))

typedef enum {
    APP_CONTROL_STATUS_OK = 0,
    APP_CONTROL_STATUS_UNSUPPORTED_OPCODE = 1,
    APP_CONTROL_STATUS_BAD_DLC = 2,
    APP_CONTROL_STATUS_BAD_ARGUMENT = 3,
    APP_CONTROL_STATUS_SCHEMA_MISMATCH = 4,
    APP_CONTROL_STATUS_UNKNOWN_SIGNAL_KEY = 5,
    APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE = 6,
    APP_CONTROL_STATUS_INVALID_STATE = 7,
    APP_CONTROL_STATUS_BUSY = 8,
    APP_CONTROL_STATUS_DUPLICATE_CONFLICT = 9,
    APP_CONTROL_STATUS_TRANSACTION_REJECTED = 10,
    APP_CONTROL_STATUS_COUNT_MISMATCH = 11,
    APP_CONTROL_STATUS_RESOURCE_FULL = 12,
    APP_CONTROL_STATUS_CATALOG_MISMATCH = 13,
    APP_CONTROL_STATUS_INTERNAL_ERROR = 14
} App_Control_Status_t;

typedef struct {
    uint32_t received;
    uint32_t ignored_endpoint;
    uint32_t ignored_response;
    uint32_t malformed_without_sequence;
    uint32_t responses_queued;
    uint32_t responses_busy;
    uint32_t responses_failed;
    uint32_t responses_deferred;
    uint32_t responses_dropped;
    uint32_t duplicate_replays;
    uint32_t duplicate_conflicts;
} App_Control_Can_Stats_t;

/* Reset protocol statistics and register the handler with the MID RX router. */
void App_Control_Can_Init(void);

/* Retry deferred control responses before the periodic generator gets a chance
 * to refill the bxCAN transmit mailboxes. */
void App_Control_Can_Process(void);

void App_Control_Can_Get_Stats(App_Control_Can_Stats_t* stats);

#ifdef __cplusplus
}
#endif
#endif /* APP_CONTROL_CAN_H */
