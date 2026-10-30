/*
 * =============================================================================
 * FILE : app_generator.c
 * WHAT : Generator state machine.
 * =============================================================================
 */

#include "app_generator.h"
#include "app_config.h"
#include "j1939_link.h"
#include "j1939_encode_decode.h"
#include "j1939_value_store.h"
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
 * PRIVATE: Compact-catalog identity and value-store helpers
 * ============================================================================= */
static bool prv_is_short_scalar(uint16_t index)
{
    if (index >= J1939_CATALOG_SIGNAL_COUNT) {
        return false;
    }
    const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[index];
    const J1939_Catalog_PGN_t* pgn = &J1939_Catalog_PGNs[sig->pgn_index];
    return ((sig->flags & J1939_CAT_SIG_BLOB) == 0u) &&
           (sig->bit_length > 0u) && (sig->bit_length <= 32u) &&
           (pgn->payload_length <= 8u) &&
           ((uint32_t)sig->start_bit + sig->bit_length <=
            (uint32_t)pgn->payload_length * 8u);
}

static uint16_t prv_find_unique_catalog_index(uint32_t spn,
                                               uint16_t* match_count)
{
    uint16_t found = J1939_CATALOG_INDEX_INVALID;
    uint16_t count = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        if (J1939_Catalog_Signals[i].spn == spn) {
            if (found == J1939_CATALOG_INDEX_INVALID) {
                found = i;
            }
            count++;
        }
    }
    if (match_count != NULL) {
        *match_count = count;
    }
    return (count == 1u) ? found : J1939_CATALOG_INDEX_INVALID;
}

/* Current physical value of an active catalog signal: store, else waveform. */
static float prv_signal_value(uint16_t catalog_index)
{
    uint32_t raw;
    if (J1939_Value_Store_Get_Raw(catalog_index, &raw)) {
        const J1939_Catalog_Scale_t* scale = &J1939_Catalog_Scales[
            J1939_Catalog_Signals[catalog_index].scale_index];
        return ((float)raw * scale->resolution) + scale->offset;
    }
    return Pattern_Generator_Get_Value(catalog_index, 0.0f);
}

/* One-record transaction on the active configuration. */
static J1939_Value_Store_Status_t prv_store_update(uint16_t signal_key,
                                                   bool active,
                                                   uint32_t raw_value)
{
    J1939_Value_Store_Status_t status =
        J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_COPY_ACTIVE);
    if (status != J1939_VALUE_STORE_OK) {
        return status;
    }
    status = active ? J1939_Value_Store_Set_Raw(signal_key, raw_value)
                    : J1939_Value_Store_Remove(signal_key);
    if (status != J1939_VALUE_STORE_OK) {
        J1939_Value_Store_Abort();
        return status;
    }
    return J1939_Value_Store_Commit(NULL);
}

static void prv_store_clear(void)
{
    J1939_Value_Store_Abort();
    (void)J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_EMPTY);
    (void)J1939_Value_Store_Commit(NULL);
}

/* Constant signals live in the value store only; any waveform for the same
 * signal is released from the pattern pool. Every check precedes mutation. */
static App_Set_Const_Status_t prv_set_constant(uint16_t index,
                                               uint32_t raw_value,
                                               uint32_t timeframe_ms,
                                               float t_start,
                                               float t_dur)
{
    (void)t_start;
    (void)t_dur;
    if (index >= J1939_CATALOG_SIGNAL_COUNT) {
        return APP_SET_CONST_UNKNOWN_KEY;
    }
    if (!prv_is_short_scalar(index)) {
        return APP_SET_CONST_UNSUPPORTED_SIGNAL;
    }

    const uint16_t key = J1939_Catalog_Signals[index].signal_key;
    switch (J1939_Value_Store_Check_Raw(key, raw_value)) {
        case J1939_VALUE_STORE_OK:
            break;
        case J1939_VALUE_STORE_VALUE_OUT_OF_RANGE:
            return APP_SET_CONST_VALUE_OUT_OF_RANGE;
        case J1939_VALUE_STORE_UNSUPPORTED_SIGNAL:
            return APP_SET_CONST_UNSUPPORTED_SIGNAL;
        default:
            return APP_SET_CONST_UNKNOWN_KEY;
    }
    if (!J1939_Sched_Can_Add_Catalog_Signal(index)) {
        return APP_SET_CONST_RESOURCE_FULL;
    }
    if (prv_store_update(key, true, raw_value) != J1939_VALUE_STORE_OK) {
        return APP_SET_CONST_RESOURCE_FULL;
    }

    (void)J1939_Sched_Add_Catalog_Signal(index, timeframe_ms, hal_time_ms());
    (void)Pattern_Generator_Unregister(index);
    return APP_SET_CONST_OK;
}

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

void App_Gen_Print_Can_Timing(void)
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
    hal_console_write(" TQ | SJW: ");
    hal_console_write_u32(t.sjw_tq);
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
#if APP_TELEMETRY_SHOW_SIGNALS
    uint16_t count = J1939_Sched_Get_Signal_Count();
    if (count == 0u) {
        return;
    }
#endif

    hal_console_write("[TX] Frames: ");
    hal_console_write_u32(s_total_frames_sent);

#if APP_TELEMETRY_SHOW_SIGNALS
    uint16_t display_count = (count <= 8u) ? count : 6u;
    for (uint16_t i = 0; i < display_count; i++) {
        uint16_t catalog_index;
        if (!J1939_Sched_Get_Active_Catalog_Index(i, &catalog_index)) {
            continue;
        }
        const J1939_Catalog_Signal_t* sig =
            &J1939_Catalog_Signals[catalog_index];
        hal_console_write(" | SPN ");
        hal_console_write_u32(sig->spn);
        hal_console_write(": ");
        hal_console_write_float(prv_signal_value(catalog_index), 1);
    }
    if (count > 8u) {
        hal_console_write(" | ... [Total: ");
        hal_console_write_u32(count);
        hal_console_write(" SPNs transmitting on CAN]");
    }
#endif
    hal_console_write_line(NULL);
}

/* =============================================================================
 * PRIVATE: Register one default waveform
 * ============================================================================= */
static void prv_add_default(uint16_t signal_key, Pattern_Type_t type,
                            float min_v, float max_v, float period_s)
{
    const uint16_t catalog_index = J1939_Catalog_Index_From_Key(signal_key);
    if (catalog_index == J1939_CATALOG_INDEX_INVALID) {
        return;
    }

    Pattern_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.catalog_index = catalog_index;
    cfg.pattern_type = type;
    cfg.min_value = min_v;
    cfg.max_value = max_v;
    cfg.initial_value = min_v;
    cfg.ramp_period_seconds = period_s;
    cfg.sine_period_seconds = period_s;
    cfg.update_interval_ms = 10u;

    if (Pattern_Generator_Register(&cfg)) {
        (void)J1939_Sched_Add_Catalog_Signal(catalog_index, 0u,
                                              hal_time_ms());
    }
}

/* =============================================================================
 * PUBLIC
 * ============================================================================= */
void App_Gen_Init(uint32_t baud_kbps)
{
    /* CAN controller is brought up by the OP lifecycle before this call */
    Pattern_Generator_Init(hal_time_ms());
    J1939_Sched_Clear();
    J1939_Value_Store_Init();
    J1939_Sched_Set_Tx_Log(APP_TXLOG_DEFAULT_ON != 0u);
    s_is_running = false;
    s_test_start_ms = 0u;
    s_test_duration_ms = 0u;
    s_total_frames_sent = 0u;
    s_baud_kbps = baud_kbps;
}

bool App_Gen_Set_Baud(uint32_t baud_kbps)
{
    s_baud_kbps = baud_kbps;
    bool ok = J1939_Link_Init(baud_kbps);
    App_Gen_Print_Can_Timing();
    return ok;
}

uint32_t App_Gen_Get_Baud(void)
{
    return s_baud_kbps;
}

App_Config_Status_t App_Gen_Config_Signal(const App_Signal_Request_t* req,
                                          uint16_t* out_catalog_index)
{
    if (out_catalog_index != NULL) {
        *out_catalog_index = J1939_CATALOG_INDEX_INVALID;
    }
    if (req == NULL) {
        return APP_CFG_UNKNOWN_SPN;
    }

    uint16_t index = J1939_CATALOG_INDEX_INVALID;
    if (req->pgn != 0u) {
        index = J1939_Catalog_Find_Signal_Index(req->pgn, req->spn);
    } else {
        uint16_t match_count = 0u;
        index = prv_find_unique_catalog_index(req->spn, &match_count);
        if (match_count > 1u) {
            return APP_CFG_AMBIGUOUS_SPN;
        }
    }

    if (index == J1939_CATALOG_INDEX_INVALID) {
        return APP_CFG_UNKNOWN_SPN;
    }
    if (out_catalog_index != NULL) {
        *out_catalog_index = index;
    }
    if (!prv_is_short_scalar(index)) {
        return APP_CFG_UNSUPPORTED_SIGNAL;
    }

    if (req->type == PATTERN_CONSTANT) {
        /* Use param1 as the constant if in range, otherwise midpoint. */
        const float physical =
            (req->param1 >= req->min_value && req->param1 <= req->max_value)
                ? req->param1
                : (req->min_value + req->max_value) * 0.5f;
        const J1939_Catalog_Signal_t* cat = &J1939_Catalog_Signals[index];
        const J1939_Catalog_Scale_t* scale = &J1939_Catalog_Scales[cat->scale_index];
        const float raw_f = (physical - scale->offset) / scale->resolution;
        uint32_t raw = cat->raw_min;
        if (raw_f >= (float)cat->raw_max) {
            raw = cat->raw_max;
        } else if (raw_f > (float)cat->raw_min) {
            raw = (uint32_t)(raw_f + 0.5f);
        }

        switch (prv_set_constant(index, raw, req->timeframe_ms,
                                 req->t_start, req->t_dur)) {
            case APP_SET_CONST_OK:
                return APP_CFG_OK;
            case APP_SET_CONST_UNSUPPORTED_SIGNAL:
                return APP_CFG_UNSUPPORTED_SIGNAL;
            case APP_SET_CONST_UNKNOWN_KEY:
                return APP_CFG_UNKNOWN_SPN;
            default:
                return APP_CFG_TABLE_FULL;
        }
    }

    if (!Pattern_Generator_Can_Register(index) ||
        !J1939_Sched_Can_Add_Catalog_Signal(index)) {
        return APP_CFG_TABLE_FULL;
    }

    Pattern_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.catalog_index = index;
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

    /* Both capacity checks above are read-only. With the single-threaded APP
     * owner, the following mutations therefore succeed as one logical
     * registration and cannot leave a half-configured signal behind. */
    if (!Pattern_Generator_Register(&cfg)) {
        return APP_CFG_TABLE_FULL;
    }
    if (!J1939_Sched_Add_Catalog_Signal(index, req->timeframe_ms,
                                        hal_time_ms())) {
        return APP_CFG_TABLE_FULL;
    }
    if (J1939_Value_Store_Is_Active(index)) {
        (void)prv_store_update(J1939_Catalog_Signals[index].signal_key, false, 0u);
    }
    return APP_CFG_OK;
}

App_Set_Const_Status_t App_Gen_Set_Constant_Raw(uint16_t signal_key,
                                                uint32_t raw_value,
                                                uint32_t* applied_raw_value)
{
    /* Direct signal_key -> catalog index mapping covers all catalog keys. */
    const uint16_t index = J1939_Catalog_Index_From_Key(signal_key);
    if (index == J1939_CATALOG_INDEX_INVALID) {
        return APP_SET_CONST_UNKNOWN_KEY;
    }

    const App_Set_Const_Status_t status =
        prv_set_constant(index, raw_value, 0u, 0.0f, 0.0f);
    if ((status == APP_SET_CONST_OK) && (applied_raw_value != NULL)) {
        *applied_raw_value = raw_value;
    }
    return status;
}

bool App_Gen_Has_Configured_Signals(void)
{
    return J1939_Sched_Get_Signal_Count() > 0u;
}

bool App_Gen_Is_Signal_Configured(uint16_t signal_key)
{
    const uint16_t catalog_index = J1939_Catalog_Index_From_Key(signal_key);
    if (catalog_index == J1939_CATALOG_INDEX_INVALID) {
        return false;
    }
    return J1939_Value_Store_Is_Active(catalog_index) ||
           Pattern_Generator_Is_Registered(catalog_index);
}

void App_Gen_Load_Defaults(void)
{
    Pattern_Generator_Init(hal_time_ms());
    J1939_Sched_Clear();
    prv_store_clear();

    prv_add_default(J1939_KEY_EEC1_SPN190,
                    PATTERN_SINE, 800.0f, 3500.0f, 8.0f);
    prv_add_default(J1939_KEY_EEC2_SPN91,
                    PATTERN_RAMP, 0.0f, 100.0f, 10.0f);
    prv_add_default(J1939_KEY_CCVS1_SPN84,
                    PATTERN_RAMP, 0.0f, 120.0f, 15.0f);
}

void App_Gen_Clear(void)
{
    Pattern_Generator_Init(hal_time_ms());
    J1939_Sched_Clear();
    prv_store_clear();
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

    /* Machine-readable run marker: waveform time t = now_ms - t0_ms */
    hal_console_write("$RUN,");
    hal_console_write_u32(now);
    hal_console_write(",");
    hal_console_write_u32((uint32_t)mode);
    hal_console_write(",");
    hal_console_write_u32(duration_sec);
    hal_console_write_line(NULL);

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
    if (s_is_running) {
        hal_console_write("$END,");
        hal_console_write_u32(hal_time_ms());
        hal_console_write(",");
        hal_console_write_u32(J1939_Sched_Get_Tx_Seq());
        hal_console_write_line(NULL);
    }
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
    uint16_t count = J1939_Sched_Get_Signal_Count();

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

    for (uint16_t i = 0; i < count; i++) {
        uint16_t catalog_index;
        if (!J1939_Sched_Get_Active_Catalog_Index(i, &catalog_index)) {
            continue;
        }
        const J1939_Catalog_Signal_t* sig =
            &J1939_Catalog_Signals[catalog_index];
        const J1939_Catalog_PGN_t* pgn =
            &J1939_Catalog_PGNs[sig->pgn_index];
        hal_console_write("  #");
        hal_console_write_u32(i + 1u);
        hal_console_write(" [Key ");
        hal_console_write_u32(sig->signal_key);
        hal_console_write("]");
        hal_console_write(": SPN ");
        hal_console_write_u32(sig->spn);
        hal_console_write(" in PGN ");
        hal_console_write_u32(pgn->pgn);
        hal_console_write(" | Cur Val: ");
        hal_console_write_float(prv_signal_value(catalog_index), 2);
        hal_console_write_line(NULL);
    }

    hal_console_write("Total Frames Sent: ");
    hal_console_write_u32(s_total_frames_sent);
    hal_console_write_line(NULL);
    hal_console_write("TX Log: ");
    hal_console_write(J1939_Sched_Get_Tx_Log() ? "ON" : "OFF");
    hal_console_write(" | Lines dropped (UART full): ");
    hal_console_write_u32(J1939_Sched_Get_Tx_Log_Dropped());
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
