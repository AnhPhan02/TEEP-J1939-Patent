/*
 * =============================================================================
 * FILE : app_generator.c
 * WHAT : Generator state machine.
 * =============================================================================
 */

#include "app_generator.h"
#include "app_config.h"
#include "j1939_link.h"
#include "hal_console.h"
#include "hal_time.h"
#include "hal_led.h"
#include <stddef.h>
#include <string.h>

static bool     s_is_running = false;
static uint32_t s_test_start_ms = 0u;
static uint32_t s_test_duration_ms = 0u;    /* 0 = continuous */
static uint32_t s_total_frames_sent = 0u;
static uint32_t s_last_telemetry_ms = 0u;
static uint32_t s_last_can_warn_ms = 0u;
static uint32_t s_baud_kbps = APP_DEFAULT_CAN_BAUD_KBPS;

/* =============================================================================
 * PRIVATE: Console reports
 * ============================================================================= */
static const char* prv_mode_name(J1939_Sched_Mode_t mode)
{
    switch (mode) {
        case J1939_SCHED_MODE_SAE:    return "SAE STANDARDS COMPLIANT";
        case J1939_SCHED_MODE_STRESS: return "STRESS TESTING (100 Hz)";
        default:                      return "HIGH-FIDELITY SMOOTH WAVEFORM (50 Hz)";
    }
}

static const char* prv_lec_text(uint8_t lec)
{
    switch (lec) {
        case 0: return "0 (No Error)";
        case 1: return "1 (Stuff Error)";
        case 2: return "2 (Form Error)";
        case 3: return "3 (ACK Error - No device is ACKing the message!)";
        case 4: return "4 (Bit Recessive Error)";
        case 5: return "5 (Bit Dominant Error)";
        case 6: return "6 (CRC Error)";
        default: return "Unknown";
    }
}

static void prv_print_can_timing(void)
{
    J1939_Link_Bit_Timing_t t;
    J1939_Link_Get_Bit_Timing(&t);

    uint32_t div = t.prescaler * t.total_tq;
    float bitrate_kbps = (div > 0u) ? (float)t.pclk_hz / (float)div / 1000.0f : 0.0f;
    float sample_pct = (t.total_tq > 0u) ? (float)(1u + t.seg1_tq) / (float)t.total_tq * 100.0f : 0.0f;

    hal_console_write_line("-----------------------------------------");
    hal_console_write("[CAN DIAG] Baud Rate Configured: ");
    hal_console_write_float(bitrate_kbps, 1);
    hal_console_write_line(" kbps");
    hal_console_write("[CAN DIAG] APB1 Clock: ");
    hal_console_write_float((float)t.pclk_hz / 1000000.0f, 3);
    hal_console_write(" MHz | Prescaler: ");
    hal_console_write_u32(t.prescaler);
    hal_console_write_line(NULL);
    hal_console_write("[CAN DIAG] TQ: 1 + ");
    hal_console_write_u32(t.seg1_tq);
    hal_console_write(" + ");
    hal_console_write_u32(t.seg2_tq);
    hal_console_write(" = ");
    hal_console_write_u32(t.total_tq);
    hal_console_write(" TQ | Sample Point: ");
    hal_console_write_float(sample_pct, 1);
    hal_console_write_line("%");
    hal_console_write_line("-----------------------------------------");
}

static void prv_print_can_busy_warning(void)
{
    J1939_Link_Bus_Error_t err;
    J1939_Link_Get_Bus_Error(&err);

    hal_console_write("[CAN WARN] Tx Mailboxes FULL (timeout)! ESR Reg: 0x");
    hal_console_write_hex(err.esr);
    hal_console_write(" | TEC: ");
    hal_console_write_u32(err.tec);
    hal_console_write(" | REC: ");
    hal_console_write_u32(err.rec);
    hal_console_write(" | Last Error Code (LEC): ");
    hal_console_write_line(prv_lec_text(err.lec));
    hal_console_write_line("  -> Check hardware connections: CANH/CANL swapped? GND connected?");
    hal_console_write_line("  -> Verify MCP2551 Pin 8 (Rs) is tied to GND for High-Speed mode!");
    hal_console_write_line("  -> Is a 120 ohm terminating resistor present on the bus?");
    hal_console_write("  -> Is TSMaster connected, active (Normal mode), and set to ");
    hal_console_write_u32(s_baud_kbps);
    hal_console_write_line(" kbps?");
}

static void prv_print_telemetry(void)
{
    uint8_t count = J1939_Sched_Get_Signal_Count();
    if (count == 0u) {
        return;
    }

    hal_console_write("[TX] Frames: ");
    hal_console_write_u32(s_total_frames_sent);

    uint8_t display_count = (count <= 8u) ? count : 6u;
    for (uint8_t i = 0; i < display_count; i++) {
        const J1939_Sched_Signal_t* sig = J1939_Sched_Get_Signal(i);
        hal_console_write(" | SPN ");
        hal_console_write_u32(sig->spn);
        hal_console_write(": ");
        hal_console_write_float(Pattern_Generator_Get_Value(sig->spn, 0.0f), 1);
        hal_console_write(" ");
        hal_console_write(sig->def->unit);
    }
    if (count > 8u) {
        hal_console_write(" | ... [Total: ");
        hal_console_write_u32(count);
        hal_console_write(" SPNs transmitting on CAN]");
    }
    hal_console_write_line(NULL);
}

/* =============================================================================
 * PRIVATE: Register one default waveform
 * ============================================================================= */
static void prv_add_default(uint32_t spn, Pattern_Type_t type, float min_v, float max_v, float period_s)
{
    const J1939_Signal_Definition_t* def = J1939_Find_Signal_By_SPN(spn);
    if (def == NULL) {
        return;
    }

    Pattern_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.spn = spn;
    cfg.pattern_type = type;
    cfg.min_value = min_v;
    cfg.max_value = max_v;
    cfg.initial_value = min_v;
    cfg.ramp_period_seconds = period_s;
    cfg.sine_period_seconds = period_s;
    cfg.update_interval_ms = 10u;

    if (Pattern_Generator_Register(&cfg)) {
        J1939_Sched_Add_Signal(def, 0u, 0.0f, 0.0f, hal_time_ms());
    }
}

/* =============================================================================
 * PUBLIC
 * ============================================================================= */
void App_Gen_Init(uint32_t baud_kbps)
{
    Pattern_Generator_Init(hal_time_ms());
    J1939_Sched_Clear();

    s_baud_kbps = baud_kbps;
    bool ok = J1939_Link_Init(baud_kbps);
    prv_print_can_timing();
    hal_console_write_line(ok ? "[SUCCESS] CAN Hardware Peripheral Initialized."
                              : "[ERROR] CAN Hardware Initialization Failed!");
}

bool App_Gen_Set_Baud(uint32_t baud_kbps)
{
    s_baud_kbps = baud_kbps;
    bool ok = J1939_Link_Init(baud_kbps);
    prv_print_can_timing();
    return ok;
}

uint32_t App_Gen_Get_Baud(void)
{
    return s_baud_kbps;
}

App_Config_Status_t App_Gen_Config_Signal(const App_Signal_Request_t* req,
                                          const J1939_Signal_Definition_t** out_def)
{
    const J1939_Signal_Definition_t* def = J1939_Find_Signal_By_SPN(req->spn);
    if (out_def != NULL) {
        *out_def = def;
    }
    if (def == NULL) {
        return APP_CFG_UNKNOWN_SPN;
    }

    Pattern_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.spn = req->spn;
    cfg.pattern_type = req->type;
    cfg.min_value = req->min_value;
    cfg.max_value = req->max_value;
    cfg.param1 = req->param1;
    cfg.timeframe_ms = req->timeframe_ms;
    cfg.t_start_sec = req->t_start;
    cfg.t_duration_sec = req->t_dur;
    cfg.update_interval_ms = (req->timeframe_ms > 0u && req->timeframe_ms < 10u) ? req->timeframe_ms : 10u;

    float period = (req->param1 > 0.1f) ? req->param1 : 10.0f;
    cfg.period_seconds = period;
    cfg.ramp_period_seconds = period;
    cfg.sine_period_seconds = period;

    switch (cfg.pattern_type) {
        case PATTERN_CONSTANT:
            /* Use param1 as the constant if in range, otherwise midpoint */
            if (req->param1 >= req->min_value && req->param1 <= req->max_value) {
                cfg.initial_value = req->param1;
            } else {
                cfg.initial_value = (req->min_value + req->max_value) * 0.5f;
            }
            break;

        case PATTERN_STEP:
            cfg.step_size = (req->max_value - req->min_value) / 8.0f;
            cfg.step_interval_ms = (uint32_t)(period * 1000.0f / 16.0f);
            cfg.initial_value = req->min_value;
            break;

        case PATTERN_RANDOM_WALK:
            cfg.initial_value = (req->min_value + req->max_value) * 0.5f;
            cfg.random_max_step = (req->max_value - req->min_value) * 0.05f;
            break;

        case PATTERN_SINE:
            cfg.initial_value = (req->min_value + req->max_value) * 0.5f;
            break;

        case PATTERN_RAMP:
        case PATTERN_TRIANGLE:
        case PATTERN_SQUARE:
        default:
            cfg.initial_value = req->min_value;
            break;
    }

    if (!Pattern_Generator_Register(&cfg)) {
        return APP_CFG_TABLE_FULL;
    }
    if (!J1939_Sched_Add_Signal(def, req->timeframe_ms, req->t_start, req->t_dur, hal_time_ms())) {
        return APP_CFG_TABLE_FULL;
    }
    return APP_CFG_OK;
}

void App_Gen_Load_Defaults(void)
{
    Pattern_Generator_Init(hal_time_ms());
    J1939_Sched_Clear();

    prv_add_default(SPN_ENGINE_SPEED,    PATTERN_SINE, 800.0f, 3500.0f,  8.0f);
    prv_add_default(SPN_ACCEL_PEDAL_POS, PATTERN_RAMP,   0.0f,  100.0f, 10.0f);
    prv_add_default(SPN_VEHICLE_SPEED,   PATTERN_RAMP,   0.0f,  120.0f, 15.0f);
}

void App_Gen_Clear(void)
{
    Pattern_Generator_Init(hal_time_ms());
    J1939_Sched_Clear();
    s_is_running = false;
}

void App_Gen_Start(uint32_t duration_sec, J1939_Sched_Mode_t mode)
{
    uint32_t now = hal_time_ms();

    J1939_Sched_Set_Mode(mode);
    J1939_Sched_Reset_Timers(now);
    Pattern_Generator_Reset(now);

    s_test_start_ms = now;
    s_test_duration_ms = duration_sec * 1000u;
    s_total_frames_sent = 0u;
    s_is_running = true;

    hal_console_write_line("=========================================");
    hal_console_write("[ACK] START OK | Mode: ");
    hal_console_write(prv_mode_name(mode));
    hal_console_write(" | Duration: ");
    if (s_test_duration_ms == 0u) {
        hal_console_write_line("CONTINUOUS (No Timeout)");
    } else {
        hal_console_write_u32(duration_sec);
        hal_console_write_line(" seconds");
    }
    hal_console_write("[ACK] Active Signals: ");
    hal_console_write_u32(J1939_Sched_Get_Signal_Count());
    hal_console_write(" | Active PGNs: ");
    hal_console_write_u32(J1939_Sched_Get_PGN_Count());
    hal_console_write_line(NULL);
    hal_console_write_line("=========================================");
}

void App_Gen_Stop(void)
{
    s_is_running = false;
    Pattern_Generator_Stop();
}

bool App_Gen_Is_Running(void)
{
    return s_is_running;
}

uint32_t App_Gen_Get_Frames_Sent(void)
{
    return s_total_frames_sent;
}

void App_Gen_Print_Status(void)
{
    uint8_t count = J1939_Sched_Get_Signal_Count();

    hal_console_write_line("----------------- STATUS -----------------");
    hal_console_write("Running: ");
    hal_console_write_line(s_is_running ? "YES" : "NO (IDLE)");
    hal_console_write("Mode: ");
    hal_console_write_line(prv_mode_name(J1939_Sched_Get_Mode()));
    hal_console_write("CAN Bitrate: ");
    hal_console_write_u32(s_baud_kbps);
    hal_console_write_line(" kbps");
    hal_console_write("Active Registered SPNs: ");
    hal_console_write_u32(count);
    hal_console_write_line(NULL);

    for (uint8_t i = 0; i < count; i++) {
        const J1939_Sched_Signal_t* sig = J1939_Sched_Get_Signal(i);
        hal_console_write("  #");
        hal_console_write_u32(i + 1u);
        hal_console_write(": SPN ");
        hal_console_write_u32(sig->spn);
        hal_console_write(" (");
        hal_console_write(sig->def->name);
        hal_console_write(") in PGN ");
        hal_console_write_u32(sig->pgn);
        hal_console_write(" | Cur Val: ");
        hal_console_write_float(Pattern_Generator_Get_Value(sig->spn, 0.0f), 2);
        hal_console_write(" ");
        hal_console_write_line(sig->def->unit);
    }

    hal_console_write("Total Frames Sent: ");
    hal_console_write_u32(s_total_frames_sent);
    hal_console_write_line(NULL);
    hal_console_write_line("------------------------------------------");
}

void App_Gen_Process(uint32_t now_ms)
{
    /* 1. Stop when the requested test duration has elapsed */
    if (s_is_running && s_test_duration_ms > 0u &&
        (now_ms - s_test_start_ms >= s_test_duration_ms)) {
        App_Gen_Stop();
        hal_console_write_line(NULL);
        hal_console_write_line("[TEST] Target test duration elapsed. Transmission completed (signals at 0).");
        hal_console_write("[STATS] Total frames sent: ");
        hal_console_write_u32(s_total_frames_sent);
        hal_console_write_line(NULL);
    }

    if (!s_is_running || J1939_Sched_Get_PGN_Count() == 0u) {
        hal_led_set(((now_ms / APP_LED_IDLE_HALF_MS) % 2u) != 0u);
        return;
    }

    /* 2. Update waveforms and broadcast due PGNs */
    Pattern_Generator_Update(now_ms);

    J1939_Sched_Tick_Result_t tick;
    J1939_Sched_Tick(now_ms, &tick);
    s_total_frames_sent += tick.sent;

    if (tick.busy > 0u && (now_ms - s_last_can_warn_ms > APP_CAN_WARN_PERIOD_MS)) {
        s_last_can_warn_ms = now_ms;
        prv_print_can_busy_warning();
    }

    /* 3. Periodic telemetry */
    if (now_ms - s_last_telemetry_ms >= APP_TELEMETRY_PERIOD_MS) {
        s_last_telemetry_ms = now_ms;
        prv_print_telemetry();
    }

    hal_led_set(((now_ms / APP_LED_ACTIVE_HALF_MS) % 2u) != 0u);
}
