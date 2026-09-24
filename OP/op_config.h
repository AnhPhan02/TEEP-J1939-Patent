/*
 * =============================================================================
 * FILE : op_config.h
 * WHAT : System lifecycle tunables (boot, supervision, recovery).
 * =============================================================================
 */

#ifndef OP_CONFIG_H
#define OP_CONFIG_H

#define OP_WATCHDOG_ENABLE          1
#define OP_WATCHDOG_TIMEOUT_MS      2000u   /* > worst-case STATUS dump (~0.5 s) */

#define OP_CAN_SELF_TEST_ENABLE     1       /* Loopback test before joining the bus */
#define OP_CAN_RETRY_PERIOD_MS      1000u   /* Re-try joining the bus in COMM_FAULT */
#define OP_BUS_MONITOR_PERIOD_MS    100u    /* Error-state supervision period */

#define OP_LED_FAULT_HALF_MS        50u     /* 10 Hz blink = CAN not started */

#endif /* OP_CONFIG_H */
