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
#define OP_CAN_RX_POLL_BUDGET       3u      /* Bound foreground RX work per loop */

/* 0: UI-controlled mode; start with an empty profile and wait for
 * SET_CONST followed by START.
 * 1: standalone demonstration mode; load and auto-start the default profile
 * once at boot, used only while no saved profile exists. A saved profile
 * with AUTOSTART also starts after a CAN join that succeeds late (COMM_FAULT
 * recovery) unless an operator START/STOP cancelled it. */
#ifndef APP_BOOT_AUTOSTART_DEFAULTS
#define APP_BOOT_AUTOSTART_DEFAULTS 0
#endif

#define OP_LED_FAULT_HALF_MS        50u     /* 10 Hz blink = CAN not started */

/* Boot-loop guard: after this many consecutive IWDG resets the saved profile
 * is restored but not auto-started (safe mode, operator START still works).
 * The streak lives in .noinit RAM and is cleared after OP_BOOT_STABLE_MS of
 * uninterrupted operation or by any non-watchdog reset. */
#define OP_WDG_SAFE_MODE_STREAK     3u
#define OP_BOOT_STABLE_MS           10000u

#endif /* OP_CONFIG_H */
