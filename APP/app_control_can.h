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
#define APP_CONTROL_OPCODE_SET_CONST     0x10u

#define APP_CONTROL_PROTOCOL_MAJOR       1u
#define APP_CONTROL_PROTOCOL_MINOR       0u
#define APP_CONTROL_SCHEMA_VERSION       1u

#define APP_CONTROL_CAPABILITY_STOP          (1u << 0)
#define APP_CONTROL_CAPABILITY_GET_STATUS    (1u << 1)
#define APP_CONTROL_CAPABILITY_DEDUP_CACHE   (1u << 2)
#define APP_CONTROL_CAPABILITY_SET_CONST     (1u << 3)
#define APP_CONTROL_CAPABILITY_START         (1u << 4)
#define APP_CONTROL_CAPABILITIES             \
    (APP_CONTROL_CAPABILITY_STOP |           \
     APP_CONTROL_CAPABILITY_GET_STATUS |     \
     APP_CONTROL_CAPABILITY_DEDUP_CACHE |    \
     APP_CONTROL_CAPABILITY_SET_CONST |      \
     APP_CONTROL_CAPABILITY_START)

#define APP_CONTROL_RUNTIME_RUNNING                  (1u << 0)
#define APP_CONTROL_RUNTIME_ENGINE_SPEED_CONFIGURED  (1u << 1)

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
    APP_CONTROL_STATUS_DUPLICATE_CONFLICT = 9
} App_Control_Status_t;

typedef struct {
    uint32_t received;
    uint32_t ignored_endpoint;
    uint32_t ignored_response;
    uint32_t malformed_without_sequence;
    uint32_t responses_queued;
    uint32_t responses_busy;
    uint32_t responses_failed;
    uint32_t duplicate_replays;
    uint32_t duplicate_conflicts;
} App_Control_Can_Stats_t;

/* Reset protocol statistics and register the handler with the MID RX router. */
void App_Control_Can_Init(void);
void App_Control_Can_Get_Stats(App_Control_Can_Stats_t* stats);

#ifdef __cplusplus
}
#endif
#endif /* APP_CONTROL_CAN_H */
