#include "app_scenario.h"
#include "app_generator.h"
#include "hal_console.h"
#include "scenario.h"
#include <stddef.h>

bool App_Scenario_Start(void)
{
    App_Gen_Clear();
    for (size_t i = 0; i < sizeof(k_scenario_signals) / sizeof(k_scenario_signals[0]); i++) {
        if (App_Gen_Config_Pattern(&k_scenario_signals[i], NULL) != APP_CFG_OK) {
            /* Never run a partially registered scenario. */
            App_Gen_Clear();
            hal_console_write_line("[SCENARIO] Configuration failed; generator idle.");
            return false;
        }
    }
    App_Gen_Set_Identity(k_scenario_id);
    J1939_Sched_Set_Tx_Log(true);
    App_Gen_Start(0u, J1939_SCHED_MODE_SMOOTH);
    return App_Gen_Is_Running();
}
