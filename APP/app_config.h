/*
 * =============================================================================
 * FILE : app_config.h
 * WHAT : Application-level tunables (no hardware mapping here - see HAL).
 * =============================================================================
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define APP_CONSOLE_BAUD            115200u
#define APP_BOOT_DELAY_MS           1500u       /* Let USB-serial settle before banner */
#define APP_CLI_BUF_LEN             128u

#define APP_DEFAULT_CAN_BAUD_KBPS   500u
#define APP_TELEMETRY_PERIOD_MS     250u
#define APP_CAN_WARN_PERIOD_MS      2000u       /* Rate limit for bus diagnostics */

#define APP_LED_ACTIVE_HALF_MS      100u        /* 5 Hz blink while transmitting */
#define APP_LED_IDLE_HALF_MS        500u        /* 1 Hz heartbeat when idle */

#endif /* APP_CONFIG_H */
