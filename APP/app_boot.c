/*
 * =============================================================================
 * FILE : app_boot.c
 * =============================================================================
 */

#include "app_boot.h"
#include "app_generator.h"
#include "hal_console.h"
#include <stddef.h>
#include <string.h>

static App_Boot_Info_t    s_info;
static J1939_Sched_Mode_t s_autostart_mode = J1939_SCHED_MODE_SAE;
static uint8_t            s_reset_cause;
static uint8_t            s_watchdog_streak;

static bool prv_bitrate_supported(uint32_t bitrate_bps)
{
    /* Same set as the CLI BAUD command; anything else cannot be joined. */
    return (bitrate_bps == 250000u) || (bitrate_bps == 500000u);
}

static void prv_fail(App_Persistence_Status_t status)
{
    /* A partially applied or CRC-mismatched profile must not be transmitted.
     * The flash image is kept for diagnosis; only RAM is cleared. */
    App_Gen_Clear();
    s_info.result = APP_BOOT_LOAD_FAILED;
    s_info.load_status = status;
    s_info.autostart = APP_AUTOSTART_OFF;
    s_info.config_crc = 0u;
    s_info.sequence = 0u;
}

void App_Boot_Restore(bool suppress_autostart, bool use_defaults_when_empty)
{
    memset(&s_info, 0, sizeof(s_info));
    s_info.result = APP_BOOT_EMPTY;
    s_info.load_status = APP_PERSISTENCE_EMPTY;
    s_info.autostart = APP_AUTOSTART_OFF;
    s_info.bitrate_kbps = App_Gen_Get_Baud();
    s_autostart_mode = J1939_SCHED_MODE_SAE;

    App_Persistence_Info_t stored;
    const App_Persistence_Status_t open = App_Persistence_Get_Info(&stored);
    if (open == APP_PERSISTENCE_EMPTY) {
        if (use_defaults_when_empty) {
            App_Gen_Load_Defaults();
            s_info.result = APP_BOOT_DEFAULTS;
            s_info.autostart = suppress_autostart ? APP_AUTOSTART_SUPPRESSED
                                                  : APP_AUTOSTART_PENDING;
            s_autostart_mode = ENABLE_WAVEFORM ? J1939_SCHED_MODE_SMOOTH
                                               : J1939_SCHED_MODE_SAE;
        }
        return;
    }
    if (open != APP_PERSISTENCE_OK) {
        prv_fail(open);
        return;
    }
    if (!prv_bitrate_supported(stored.stored.bitrate_bps)) {
        prv_fail(APP_PERSISTENCE_UNSUPPORTED_CONFIG);
        return;
    }

    const App_Persistence_Status_t load =
        App_Persistence_Load(NULL, NULL, NULL);
    if (load != APP_PERSISTENCE_OK) {
        prv_fail(load);
        return;
    }

    s_info.result = APP_BOOT_LOADED;
    s_info.load_status = APP_PERSISTENCE_OK;
    s_info.bitrate_kbps = stored.stored.bitrate_bps / 1000u;
    s_info.config_crc = stored.stored.config_crc;
    s_info.sequence = stored.stored.sequence;
    if (stored.autostart) {
        s_info.autostart = suppress_autostart ? APP_AUTOSTART_SUPPRESSED
                                              : APP_AUTOSTART_PENDING;
    }
}

uint32_t App_Boot_Get_Bitrate_Kbps(void)
{
    return s_info.bitrate_kbps;
}

void App_Boot_Set_Reset_Info(uint8_t reset_cause, uint8_t watchdog_streak)
{
    s_reset_cause = reset_cause;
    s_watchdog_streak = watchdog_streak;
}

bool App_Boot_Try_Autostart(void)
{
    if (s_info.autostart != APP_AUTOSTART_PENDING) {
        return false;
    }
    /* Same admission gate as the CAN START command; a rejected profile is
     * reported instead of silently not transmitting. */
    if (!App_Gen_Active_Profile_Can_Start()) {
        s_info.autostart = APP_AUTOSTART_NOT_STARTABLE;
        hal_console_write_line("[BOOT] Autostart rejected - profile is not startable.");
        return false;
    }
    App_Gen_Start(0u, s_autostart_mode);
    s_info.autostart = APP_AUTOSTART_STARTED;
    hal_console_write_line("[BOOT] Autostart - restored profile transmitting.");
    return true;
}

void App_Boot_Cancel_Autostart(void)
{
    if (s_info.autostart == APP_AUTOSTART_PENDING) {
        s_info.autostart = APP_AUTOSTART_CANCELLED;
    }
}

void App_Boot_Get_Info(App_Boot_Info_t* out)
{
    if (out != NULL) {
        *out = s_info;
        out->reset_cause = s_reset_cause;
        out->watchdog_streak = s_watchdog_streak;
    }
}
