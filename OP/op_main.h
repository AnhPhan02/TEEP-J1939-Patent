/*
 * =============================================================================
 * FILE : op_main.h
 * WHAT : System lifecycle - main() drives the firmware through these states:
 *
 *   RESET -> SYSTEM_INIT -> BOARD_INIT -> APP_INIT -> COMM_INIT -> RUN
 *                                                        |          ^
 *                                                        +--> COMM_FAULT
 *                                                            (retry CAN join)
 *
 *        APP_INIT restores the saved profile before COMM_INIT because the
 *        profile carries the CAN bitrate. A stored AUTOSTART starts on the
 *        first successful join, including a join from COMM_FAULT.
 * =============================================================================
 */

#ifndef OP_MAIN_H
#define OP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    OP_STATE_RESET = 0,
    OP_STATE_SYSTEM_INIT,   /* Clock tree, FLASH wait states, SysTick */
    OP_STATE_BOARD_INIT,    /* LED, console, boot report */
    OP_STATE_APP_INIT,      /* Generator init, restore saved A/B profile */
    OP_STATE_COMM_INIT,     /* CAN self test, join the bus, autostart */
    OP_STATE_RUN,           /* Normal operation */
    OP_STATE_COMM_FAULT     /* CAN could not start - CLI alive, periodic retry */
} OP_State_t;

OP_State_t  OP_Get_State(void);
const char* OP_State_Name(OP_State_t state);

/* main() is OP_Init() followed by OP_Step(hal_time_ms()) forever. Host tests
 * build with -DOP_NO_MAIN and call both directly to simulate power cycles. */
void OP_Init(void);
void OP_Step(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* OP_MAIN_H */
