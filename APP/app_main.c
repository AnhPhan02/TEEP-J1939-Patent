/*
 * =============================================================================
 * FILE : app_main.c
 * WHAT : J1939 DYNAMIC SERIAL & WAVEFORM GENERATOR FIRMWARE - entry points.
 *   Creators: Vishal Meyyappan R (3rd Year ECE - ACT) & Srikar (4th year ECE)
 *   Institution: Chennai Institute of Technology (CIT Chennai) & STUST
 * =============================================================================
 */

#include "app_main.h"
#include "app_config.h"
#include "app_cli.h"
#include "app_generator.h"
#include "hal_console.h"
#include "hal_time.h"
#include "hal_led.h"
#include <stddef.h>

void App_Init(void)
{
    hal_console_init(APP_CONSOLE_BAUD);
    hal_delay_ms(APP_BOOT_DELAY_MS);

    hal_console_write_line(NULL);
    hal_console_write_line("=======================================================");
    hal_console_write_line("  J1939 DYNAMIC WAVEFORM GENERATOR & VERIFICATION HUB  ");
    hal_console_write_line("  Chennai Institute of Technology (CIT Chennai) & STUST");
    hal_console_write_line("  Creators: Vishal Meyyappan R (3rd Year ECE - ACT)");
    hal_console_write_line("            Srikar (4th year ECE)");
    hal_console_write_line("=======================================================");

    hal_led_init();
    App_Gen_Init(APP_DEFAULT_CAN_BAUD_KBPS);

    /* Auto-start default J1939 powertrain signals immediately on boot */
    App_Gen_Load_Defaults();
    App_Gen_Start(0u, J1939_SCHED_MODE_SMOOTH);

    hal_console_write_line("[READY] Auto-started default J1939 transmission (EEC1, EEC2, CCVS1).");
    hal_console_write_line("        Connect Web Dashboard to customize waveforms or view live telemetry.");
    hal_console_write_line("-------------------------------------------------------");
}

void App_Run(void)
{
    App_Cli_Poll();
    App_Gen_Process(hal_time_ms());
}
