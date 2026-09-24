/*
 * =============================================================================
 * FILE : op_main.c
 * WHAT : Firmware entry point and system lifecycle.
 *        J1939 DYNAMIC WAVEFORM GENERATOR & VERIFICATION HUB
 *   Creators: Vishal Meyyappan R (3rd Year ECE - ACT) & Srikar (4th year ECE)
 *   Institution: Chennai Institute of Technology (CIT Chennai) & STUST
 * =============================================================================
 */

#include "op_main.h"
#include "op_config.h"
#include "app_config.h"
#include "app_cli.h"
#include "app_generator.h"
#include "j1939_link.h"
#include "hal_system.h"
#include "hal_time.h"
#include "hal_console.h"
#include "hal_led.h"
#include "hal_can.h"
#include <stddef.h>

typedef enum {
    BUS_ERROR_ACTIVE = 0,
    BUS_ERROR_WARNING,
    BUS_ERROR_PASSIVE,
    BUS_OFF
} op_bus_level_t;

static OP_State_t     s_state = OP_STATE_RESET;
static op_bus_level_t s_bus_level = BUS_ERROR_ACTIVE;
static uint32_t       s_last_bus_check_ms = 0u;
static uint32_t       s_last_can_retry_ms = 0u;

/* =============================================================================
 * STATE NAMES
 * ============================================================================= */
const char* OP_State_Name(OP_State_t state)
{
    switch (state) {
        case OP_STATE_RESET:       return "RESET";
        case OP_STATE_SYSTEM_INIT: return "SYSTEM_INIT";
        case OP_STATE_BOARD_INIT:  return "BOARD_INIT";
        case OP_STATE_COMM_INIT:   return "COMM_INIT";
        case OP_STATE_APP_INIT:    return "APP_INIT";
        case OP_STATE_RUN:         return "RUN";
        case OP_STATE_COMM_FAULT:  return "COMM_FAULT";
        default:                   return "?";
    }
}

OP_State_t OP_Get_State(void)
{
    return s_state;
}

static void prv_enter(OP_State_t next)
{
    s_state = next;
}

/* =============================================================================
 * 1. SYSTEM_INIT - clock tree first, everything else depends on it
 * ============================================================================= */
static void prv_system_init(void)
{
    (void)hal_system_init();                /* HSE->PLL 72 MHz, APB1 36 MHz (fallback HSI 64/32) */

    hal_clock_info_t clk;
    hal_system_get_clocks(&clk);
    hal_time_init(clk.hclk_hz);             /* 1 ms SysTick */
}

/* =============================================================================
 * 2. BOARD_INIT - LED, console, boot report
 * ============================================================================= */
static void prv_print_mhz(const char* label, uint32_t hz)
{
    hal_console_write(label);
    hal_console_write_u32(hz / 1000000u);
    hal_console_write(" MHz");
}

static void prv_board_init(void)
{
    hal_led_init();
    hal_console_init(APP_CONSOLE_BAUD);

    hal_clock_info_t clk;
    hal_system_get_clocks(&clk);

    hal_console_write_line(NULL);
    hal_console_write_line("=======================================================");
    hal_console_write_line("  J1939 DYNAMIC WAVEFORM GENERATOR & VERIFICATION HUB  ");
    hal_console_write_line("  Chennai Institute of Technology (CIT Chennai) & STUST");
    hal_console_write_line("  Creators: Vishal Meyyappan R (3rd Year ECE - ACT)");
    hal_console_write_line("            Srikar (4th year ECE)");
    hal_console_write_line("=======================================================");

    hal_console_write("[BOOT] Reset cause: ");
    hal_console_write_line(hal_system_reset_cause_name(hal_system_get_reset_cause()));

    hal_console_write("[BOOT] Clock source: ");
    switch (clk.source) {
        case HAL_CLOCK_SRC_HSE_PLL: hal_console_write_line("HSE 8 MHz x PLL"); break;
        case HAL_CLOCK_SRC_HSI_PLL: hal_console_write_line("HSI/2 x PLL (WARNING: HSE crystal did not start)"); break;
        default:                    hal_console_write_line("HSI 8 MHz (WARNING: PLL did not lock)"); break;
    }
    prv_print_mhz("[BOOT] SYSCLK ", clk.sysclk_hz);
    prv_print_mhz(" | AHB ", clk.hclk_hz);
    prv_print_mhz(" | APB1 ", clk.pclk1_hz);
    prv_print_mhz(" | APB2 ", clk.pclk2_hz);
    hal_console_write_line(NULL);
}

/* =============================================================================
 * 3. COMM_INIT - CAN self test, then join the bus
 * ============================================================================= */
static bool prv_can_join(void)
{
    bool ok = J1939_Link_Init(APP_DEFAULT_CAN_BAUD_KBPS);
    App_Gen_Print_Can_Timing();
    hal_console_write_line(ok ? "[CAN] Controller started - on bus (error active)."
                              : "[CAN] ERROR: controller did not leave init mode (bus stuck dominant? bitrate not derivable from APB1?)");
    return ok;
}

static bool prv_comm_init(void)
{
#if OP_CAN_SELF_TEST_ENABLE
    bool self_test_ok = hal_can_self_test(APP_DEFAULT_CAN_BAUD_KBPS * 1000u);
    hal_console_write("[CAN] Self test (loopback + silent): ");
    hal_console_write_line(self_test_ok ? "PASS" : "FAIL - check clock plan / bit timing");
#endif
    return prv_can_join();
}

/* =============================================================================
 * 4. APP_INIT - generator defaults, auto start when CAN is up
 * ============================================================================= */
static void prv_app_init(bool can_ok)
{
    App_Gen_Init(APP_DEFAULT_CAN_BAUD_KBPS);
    App_Gen_Load_Defaults();

    if (can_ok) {
        App_Gen_Start(0u, J1939_SCHED_MODE_SMOOTH);
        hal_console_write_line("[READY] Auto-started default J1939 transmission (EEC1, EEC2, CCVS1).");
    } else {
        hal_console_write_line("[READY] CAN not available - CLI active, retrying bus join.");
    }
    hal_console_write_line("-------------------------------------------------------");
}

/* =============================================================================
 * 5. RUN - bus error-state supervision (recovery itself is automatic: ABOM=1)
 * ============================================================================= */
static void prv_supervise_bus(uint32_t now_ms)
{
    if (now_ms - s_last_bus_check_ms < OP_BUS_MONITOR_PERIOD_MS) {
        return;
    }
    s_last_bus_check_ms = now_ms;

    J1939_Link_Bus_Error_t err;
    J1939_Link_Get_Bus_Error(&err);

    op_bus_level_t level = err.bus_off       ? BUS_OFF :
                           err.error_passive ? BUS_ERROR_PASSIVE :
                           err.error_warning ? BUS_ERROR_WARNING : BUS_ERROR_ACTIVE;
    if (level == s_bus_level) {
        return;
    }
    s_bus_level = level;

    static const char* const k_names[] = { "ERROR ACTIVE (recovered)", "ERROR WARNING", "ERROR PASSIVE", "BUS-OFF (auto recovery pending)" };
    hal_console_write("[CAN] Bus state -> ");
    hal_console_write(k_names[level]);
    hal_console_write(" | TEC: ");
    hal_console_write_u32(err.tec);
    hal_console_write(" | REC: ");
    hal_console_write_u32(err.rec);
    hal_console_write_line(NULL);
}

/* =============================================================================
 * COMM_FAULT - keep the CLI alive, re-try the bus periodically
 * ============================================================================= */
static void prv_comm_fault_step(uint32_t now_ms)
{
    hal_led_set(((now_ms / OP_LED_FAULT_HALF_MS) % 2u) != 0u);

    if (now_ms - s_last_can_retry_ms < OP_CAN_RETRY_PERIOD_MS) {
        return;
    }
    s_last_can_retry_ms = now_ms;

    if (prv_can_join()) {
        App_Gen_Start(0u, J1939_SCHED_MODE_SMOOTH);
        prv_enter(OP_STATE_RUN);
    }
}

/* =============================================================================
 * ENTRY POINT
 * ============================================================================= */
int main(void)
{
    prv_enter(OP_STATE_SYSTEM_INIT);
    prv_system_init();

    prv_enter(OP_STATE_BOARD_INIT);
    prv_board_init();

    prv_enter(OP_STATE_COMM_INIT);
    bool can_ok = prv_comm_init();

    prv_enter(OP_STATE_APP_INIT);
    prv_app_init(can_ok);

#if OP_WATCHDOG_ENABLE
    hal_watchdog_start(OP_WATCHDOG_TIMEOUT_MS);
#endif

    s_last_can_retry_ms = hal_time_ms();
    prv_enter(can_ok ? OP_STATE_RUN : OP_STATE_COMM_FAULT);

    for (;;) {
        uint32_t now = hal_time_ms();

#if OP_WATCHDOG_ENABLE
        hal_watchdog_feed();
#endif
        App_Cli_Poll();

        if (s_state == OP_STATE_RUN) {
            App_Gen_Process(now);
            prv_supervise_bus(now);
        } else {
            prv_comm_fault_step(now);
        }
    }
}
