/*
 * =============================================================================
 * FILE : op_main.c
 * =============================================================================
 */

#include "op_main.h"
#include "op_config.h"
#include "app_config.h"
#if APP_CLI_ENABLE
#include "app_cli.h"
#endif
#include "app_boot.h"
#include "app_control_can.h"
#include "app_generator.h"
#include "j1939_link.h"
#include "j1939_rx_router.h"
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

/* Survives a watchdog/software reset: the startup code initializes neither
 * .data nor .bss into .noinit. The magic rejects power-on garbage. */
#define OP_NOINIT_MAGIC  0xB007C0DEu
__attribute__((section(".noinit"))) static uint32_t s_noinit_magic;
__attribute__((section(".noinit"))) static uint32_t s_wdg_streak;

/* =============================================================================
 * STATE NAMES
 * ============================================================================= */
const char* OP_State_Name(OP_State_t state)
{
    switch (state) {
        case OP_STATE_RESET:       return "RESET";
        case OP_STATE_SYSTEM_INIT: return "SYSTEM_INIT";
        case OP_STATE_BOARD_INIT:  return "BOARD_INIT";
        case OP_STATE_APP_INIT:    return "APP_INIT";
        case OP_STATE_COMM_INIT:   return "COMM_INIT";
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
    hal_console_write_line("-----------------------VIETNAM TEAM--------------------");
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
 * 3. APP_INIT - restore the saved A/B profile before CAN: it owns the bitrate
 * ============================================================================= */
static void prv_report_boot(void)
{
    App_Boot_Info_t boot;
    App_Boot_Get_Info(&boot);

    hal_console_write("[BOOT] Profile: ");
    switch (boot.result) {
        case APP_BOOT_LOADED:
            hal_console_write("restored seq ");
            hal_console_write_u32(boot.sequence);
            hal_console_write(" | CRC 0x");
            hal_console_write_hex(boot.config_crc);
            break;
        case APP_BOOT_LOAD_FAILED:
            hal_console_write("LOAD FAILED (status ");
            hal_console_write_u32((uint32_t)boot.load_status);
            hal_console_write(") - stays STOPPED, flash kept");
            break;
        case APP_BOOT_DEFAULTS:
            hal_console_write("storage empty - demo defaults (EEC1, EEC2, CCVS1)");
            break;
        default:
            hal_console_write("storage empty - waiting for UI configuration");
            break;
    }
    hal_console_write(" | Autostart: ");
    hal_console_write_line((boot.autostart == APP_AUTOSTART_PENDING) ? "ON" :
                           (boot.autostart == APP_AUTOSTART_SUPPRESSED) ? "SUPPRESSED" : "OFF");
}

/* Counts consecutive IWDG resets; true once the boot-loop limit is hit. */
static bool prv_boot_loop_detected(hal_reset_cause_t cause)
{
    if (s_noinit_magic != OP_NOINIT_MAGIC) {
        s_noinit_magic = OP_NOINIT_MAGIC;
        s_wdg_streak = 0u;
    }
    if (cause != HAL_RESET_IWDG) {
        s_wdg_streak = 0u;
    } else if (s_wdg_streak < UINT8_MAX) {
        s_wdg_streak++;
    }
    return s_wdg_streak >= OP_WDG_SAFE_MODE_STREAK;
}

static void prv_app_init(void)
{
    const hal_reset_cause_t cause = hal_system_get_reset_cause();
    const bool safe_mode = prv_boot_loop_detected(cause);

    J1939_Rx_Router_Init();
    App_Control_Can_Init();                 /* also initializes persistence */
    App_Gen_Init(APP_DEFAULT_CAN_BAUD_KBPS);
    App_Boot_Restore(safe_mode, APP_BOOT_AUTOSTART_DEFAULTS != 0);
    App_Boot_Set_Reset_Info((uint8_t)cause, (uint8_t)s_wdg_streak);
    if (safe_mode) {
        hal_console_write("[BOOT] SAFE MODE - consecutive watchdog resets: ");
        hal_console_write_u32(s_wdg_streak);
        hal_console_write_line(NULL);
    }
    prv_report_boot();
}

/* =============================================================================
 * 4. COMM_INIT - CAN self test, then join the bus at the restored bitrate
 * ============================================================================= */
static bool prv_can_join(void)
{
    /* Records the bitrate for SAVE/STATUS and prints the bit timing. */
    bool ok = App_Gen_Set_Baud(App_Boot_Get_Bitrate_Kbps());
    hal_console_write_line(ok ? "[CAN] Controller started - on bus (error active)."
                              : "[CAN] ERROR: controller did not leave init mode (bus stuck dominant? bitrate not derivable from APB1?)");
    return ok;
}

static bool prv_comm_init(void)
{
#if OP_CAN_SELF_TEST_ENABLE
    bool self_test_ok = hal_can_self_test(App_Boot_Get_Bitrate_Kbps() * 1000u);
    hal_console_write("[CAN] Self test (loopback + silent): ");
    hal_console_write_line(self_test_ok ? "PASS" : "FAIL - check clock plan / bit timing");
#endif
    return prv_can_join();
}

static void prv_report_ready(bool can_ok)
{
    if (!can_ok) {
        hal_console_write_line("[READY] CAN not available - profile kept in RAM, retrying bus join.");
    } else if (!App_Boot_Try_Autostart()) {
        hal_console_write_line("[READY] CAN active - generator STOPPED, waiting for a control command.");
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
    /* Leaving error-passive/bus-off means frames are ACKed again. Stale
     * frames were already discarded by the scheduler; only count the event. */
    const bool recovered = (s_bus_level >= BUS_ERROR_PASSIVE) &&
                           (level < BUS_ERROR_PASSIVE);
    s_bus_level = level;
    if (recovered) {
        App_Gen_Note_Bus_Recovered();
    }

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
        prv_enter(OP_STATE_RUN);
        /* Only a stored AUTOSTART that no operator START/STOP cancelled may
         * resume generation; otherwise recovery leaves the generator STOPPED. */
        if (!App_Boot_Try_Autostart()) {
            App_Gen_Stop();
            hal_console_write_line("[READY] CAN recovered - generator STOPPED, waiting for a control command.");
        }
    }
}

/* =============================================================================
 * ENTRY POINT
 * ============================================================================= */
void OP_Init(void)
{
    s_bus_level = BUS_ERROR_ACTIVE;
    s_last_bus_check_ms = 0u;

    prv_enter(OP_STATE_SYSTEM_INIT);
    prv_system_init();

    prv_enter(OP_STATE_BOARD_INIT);
    prv_board_init();

    prv_enter(OP_STATE_APP_INIT);
    prv_app_init();

    prv_enter(OP_STATE_COMM_INIT);
    bool can_ok = prv_comm_init();
    prv_report_ready(can_ok);

#if OP_WATCHDOG_ENABLE
    hal_watchdog_start(OP_WATCHDOG_TIMEOUT_MS);
#endif

    s_last_can_retry_ms = hal_time_ms();
    prv_enter(can_ok ? OP_STATE_RUN : OP_STATE_COMM_FAULT);
}

void OP_Step(uint32_t now_ms)
{
#if OP_WATCHDOG_ENABLE
    hal_watchdog_feed();
#endif
    if ((s_wdg_streak != 0u) && (now_ms >= OP_BOOT_STABLE_MS)) {
        s_wdg_streak = 0u;                  /* ran long enough: not a boot loop */
    }
#if APP_CLI_ENABLE
    App_Cli_Poll();
#endif

    if (s_state == OP_STATE_RUN) {
        /* RX service remains active even when the generator is STOPPED. */
        J1939_Rx_Router_Poll(OP_CAN_RX_POLL_BUDGET);
        /* Control ACKs have priority over periodic generator traffic. */
        App_Control_Can_Process();
        App_Gen_Process(now_ms);
        prv_supervise_bus(now_ms);
    }
    else {
        prv_comm_fault_step(now_ms);
    }
}

#ifndef OP_NO_MAIN
int main(void)
{
    OP_Init();
    while(1U) {
        OP_Step(hal_time_ms());
    }
}
#endif
