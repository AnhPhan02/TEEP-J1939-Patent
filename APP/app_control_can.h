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
} App_Control_Can_Stats_t;

/* Reset protocol statistics and register the handler with the MID RX router. */
void App_Control_Can_Init(void);
void App_Control_Can_Get_Stats(App_Control_Can_Stats_t* stats);

#ifdef __cplusplus
}
#endif
#endif /* APP_CONTROL_CAN_H */
