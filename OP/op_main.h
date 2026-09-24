/*
 * =============================================================================
 * FILE : op_main.h
 * WHAT : System lifecycle - main() drives the firmware through these states:
 *
 *   RESET -> SYSTEM_INIT -> BOARD_INIT -> COMM_INIT -> APP_INIT -> RUN
 *                                              |                    ^
 *                                              +--> COMM_FAULT -----+
 *                                                  (retry CAN join)
 * =============================================================================
 */

#ifndef OP_MAIN_H
#define OP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OP_STATE_RESET = 0,
    OP_STATE_SYSTEM_INIT,   /* Clock tree, FLASH wait states, SysTick */
    OP_STATE_BOARD_INIT,    /* LED, console, boot report */
    OP_STATE_COMM_INIT,     /* CAN self test, join the bus */
    OP_STATE_APP_INIT,      /* J1939 generator defaults, auto start */
    OP_STATE_RUN,           /* Normal operation */
    OP_STATE_COMM_FAULT     /* CAN could not start - CLI alive, periodic retry */
} OP_State_t;

OP_State_t  OP_Get_State(void);
const char* OP_State_Name(OP_State_t state);

#ifdef __cplusplus
}
#endif
#endif /* OP_MAIN_H */
