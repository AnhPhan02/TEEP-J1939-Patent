/*
 * =============================================================================
 * FILE : app_boot.h
 * WHAT : Phase 7 standalone boot - restore the newest valid A/B profile into
 *        RAM before CAN is joined, then start it once the bus is available
 *        when the stored AUTOSTART flag is set.
 *
 *   App_Gen_Init -> App_Persistence_Init -> App_Boot_Restore
 *       -> CAN join at App_Boot_Get_Bitrate_Kbps() -> App_Boot_Try_Autostart
 *
 *        An explicit START/STOP command cancels a pending autostart, so a CAN
 *        recovery never overrides an operator decision.
 * =============================================================================
 */

#ifndef APP_BOOT_H
#define APP_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "app_persistence.h"
#include "j1939_tx_scheduler.h"

typedef enum {
    APP_BOOT_EMPTY = 0,      /* no saved profile, RAM profile empty */
    APP_BOOT_LOADED,         /* saved profile restored and CRC verified */
    APP_BOOT_LOAD_FAILED,    /* invalid/mismatched image; RAM cleared, flash kept */
    APP_BOOT_DEFAULTS        /* storage empty, compile-time demo defaults loaded */
} App_Boot_Result_t;

typedef enum {
    APP_AUTOSTART_OFF = 0,       /* flag not set (or nothing loaded) */
    APP_AUTOSTART_PENDING,       /* waiting for the CAN controller */
    APP_AUTOSTART_STARTED,
    APP_AUTOSTART_CANCELLED,     /* explicit START/STOP before CAN came up */
    APP_AUTOSTART_SUPPRESSED,    /* caller asked for safe mode */
    APP_AUTOSTART_NOT_STARTABLE  /* e.g. TP/VIN profile, rejected by START gate */
} App_Autostart_State_t;

typedef struct {
    App_Boot_Result_t result;
    App_Persistence_Status_t load_status;
    App_Autostart_State_t autostart;
    uint32_t bitrate_kbps;       /* bitrate the bus must be joined at */
    uint32_t config_crc;         /* stored CRC when LOADED, else 0 */
    uint32_t sequence;           /* stored slot sequence when LOADED, else 0 */
    uint8_t reset_cause;         /* hal_reset_cause_t latched by OP */
    uint8_t watchdog_streak;     /* consecutive IWDG resets before this boot */
} App_Boot_Info_t;

/* Call after App_Gen_Init and App_Persistence_Init, before the CAN join.
 * suppress_autostart restores the profile but never starts it (safe mode).
 * use_defaults_when_empty loads App_Gen_Load_Defaults for an empty store. */
void App_Boot_Restore(bool suppress_autostart, bool use_defaults_when_empty);

uint32_t App_Boot_Get_Bitrate_Kbps(void);

/* OP reports why this boot happened (reported by GET_STATUS page 8). */
void App_Boot_Set_Reset_Info(uint8_t reset_cause, uint8_t watchdog_streak);

/* Call whenever the CAN controller is (again) on the bus. Starts a pending
 * autostart once; returns true only if this call started the generator. */
bool App_Boot_Try_Autostart(void);

/* Operator START/STOP: a pending autostart must not fire afterwards. */
void App_Boot_Cancel_Autostart(void);

void App_Boot_Get_Info(App_Boot_Info_t* out);

#ifdef __cplusplus
}
#endif
#endif /* APP_BOOT_H */
