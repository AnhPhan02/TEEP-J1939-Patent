/* Host execution of production pattern/packing/scheduling code, with only I/O mocked. */
#include "j1939_tx_scheduler.h"
#include "j1939_encode_decode.h"
#include "j1939_link.h"
#include "hal_console.h"
#include "app_generator.h"
#include "app_scenario.h"
#include "app_cli.h"
#include "scenario.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t clock_ms;
static const char *serial_input = "";
static char console[32768];
static unsigned frames;
static uint8_t last_data[8];
static uint32_t last_id;
static J1939_Link_Status_t send_status;
static uint16_t uart_free = 1024u;

J1939_Link_Status_t J1939_Link_Send(uint32_t pgn, const uint8_t *data, uint8_t length,
                                    uint8_t priority, uint8_t sa)
{
    assert(length == 8);
    memcpy(last_data, data, 8);
    J1939_Frame_Header_t h = { .pgn = pgn, .priority = priority,
                              .source_address = sa, .destination_address = 255 };
    last_id = J1939_Frame_Build_Id(&h);
    frames++;
    return send_status;
}
void hal_console_write(const char *s)
{
    if (s) {
        assert(strlen(console) + strlen(s) < sizeof(console));
        strcat(console, s);
    }
}
void hal_console_write_line(const char *s) { hal_console_write(s); hal_console_write("\n"); }
void hal_console_write_u32(uint32_t value) { char s[32]; snprintf(s, sizeof(s), "%u", value); hal_console_write(s); }
void hal_console_write_i32(int32_t value) { char s[32]; snprintf(s, sizeof(s), "%d", value); hal_console_write(s); }
void hal_console_write_hex(uint32_t value) { char s[32]; snprintf(s, sizeof(s), "%x", value); hal_console_write(s); }
void hal_console_write_float(float value, uint8_t decimals) { (void)decimals; char s[32]; snprintf(s, sizeof(s), "%.3f", (double)value); hal_console_write(s); }
int hal_console_read(void) { return *serial_input ? (unsigned char)*serial_input++ : -1; }
uint32_t hal_time_ms(void) { return clock_ms; }
void hal_led_set(bool on) { (void)on; }
bool J1939_Link_Init(uint32_t baud) { (void)baud; return true; }
void J1939_Link_Get_Bit_Timing(J1939_Link_Bit_Timing_t *out) { memset(out, 0, sizeof(*out)); }
void J1939_Link_Get_Bus_Error(J1939_Link_Bus_Error_t *out) { memset(out, 0, sizeof(*out)); }
void J1939_Link_Diagnose(J1939_Link_Diag_t *out) { memset(out, 0, sizeof(*out)); }
uint16_t hal_console_tx_free(void) { return uart_free; }

static void setup(uint32_t origin)
{
    App_Gen_Clear();
    console[0] = 0;
    clock_ms = origin;
    Pattern_Generator_Init(origin);
    J1939_Sched_Clear();
    J1939_Sched_Set_Tx_Log(false);
    frames = 0;
    send_status = J1939_LINK_OK;
    uart_free = 1024;
}

static void add(uint32_t spn, uint32_t start, uint32_t duration, uint32_t period, float value)
{
    Pattern_Config_t cfg = { .spn = spn, .pattern_type = PATTERN_CONSTANT,
                             .min_value = value, .max_value = value, .param1 = value,
                             .start_ms = start, .duration_ms = duration };
    assert(Pattern_Generator_Register(&cfg));
    assert(J1939_Sched_Add_Signal(J1939_Find_Signal_By_SPN(spn), period, start, duration, 0));
}

static void tick(uint32_t now)
{
    Pattern_Generator_Update(now);
    J1939_Sched_Tick(now, NULL);
}

static void test_active_windows(void)
{
    setup(1000);
    add(190, 7, 40, 20, 1500);
    J1939_Sched_Reset_Timers(1000);
    tick(1006); assert(frames == 0);
    tick(1007); assert(frames == 1);
    assert(last_id == 0x0CF00400);
    assert(last_data[3] == 0xE0 && last_data[4] == 0x2E);
    tick(1026); assert(frames == 1);
    tick(1027); assert(frames == 2);
    tick(1047); assert(frames == 2);
    assert(J1939_Sched_Is_Complete(1047));
    J1939_Sched_Reset_Timers(2000);
    Pattern_Generator_Reset(2000);
    tick(2007); assert(frames == 3);
}

static void test_shared_fields(void)
{
    setup(0);
    add(91, 0, 100, 20, 40);
    add(92, 40, 100, 20, 50);
    J1939_Sched_Reset_Timers(0);
    tick(0);
    assert(last_id == 0x0CF00300);
    assert(last_data[1] == 100 && last_data[2] == 255);
    tick(40);
    assert(last_data[1] == 100 && last_data[2] == 50);
    tick(100);
    assert(last_data[1] == 255 && last_data[2] == 50);
    tick(140);
    assert(frames == 3 && J1939_Sched_Is_Complete(140));
}

static void test_deadlines_and_errors(void)
{
    setup(0);
    add(190, 0, 0, 20, 1500);
    J1939_Sched_Reset_Timers(0);
    tick(0); tick(65); assert(frames == 2);
    J1939_Sched_Stats_t stats;
    J1939_Sched_Get_Stats(&stats);
    assert(stats.missed_deadlines == 2);
    tick(79); assert(frames == 2);
    send_status = J1939_LINK_BUSY;
    tick(80);
    send_status = J1939_LINK_ERROR;
    uart_free = 0;
    J1939_Sched_Set_Tx_Log(true);
    tick(100);
    J1939_Sched_Get_Stats(&stats);
    assert(stats.queued == 2 && stats.busy == 1 && stats.failed == 1);
    assert(J1939_Sched_Get_Tx_Log_Dropped() == 1);
    assert(!J1939_Sched_Is_Complete(100));
}

static void test_inactive_gap(void)
{
    setup(0);
    add(91, 0, 20, 20, 40);
    add(92, 100, 20, 20, 50);
    J1939_Sched_Reset_Timers(0);
    tick(0); tick(100);
    J1939_Sched_Stats_t stats;
    J1939_Sched_Get_Stats(&stats);
    assert(stats.missed_deadlines == 0 && frames == 2);
}

static void test_wrap_and_long_run(void)
{
    uint32_t origin = UINT32_MAX - 9u;
    setup(origin);
    add(190, 0, 0, 20, 1500);
    J1939_Sched_Reset_Timers(origin);
    tick(origin); tick(10u); assert(frames == 2);
    /* Accumulated elapsed time remains correct across another full tick cycle. */
    tick(UINT32_MAX - 10u); tick(20u);
    assert(frames == 4);
}

static void test_patterns(void)
{
    Pattern_Type_t types[] = { PATTERN_RAMP, PATTERN_SINE, PATTERN_TRIANGLE,
                              PATTERN_SQUARE, PATTERN_STEP };
    float at_quarter[] = { 25, 100, 50, 100, 100.0f / 3.0f };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        Pattern_Generator_Init(1000);
        Pattern_Config_t cfg = { .spn = 190, .pattern_type = types[i],
                                 .min_value = 0, .max_value = 100, .param1 = 4,
                                 .start_ms = 7, .waveform_period_ms = 1000 };
        assert(Pattern_Generator_Register(&cfg));
        Pattern_Generator_Update(1257);
        float value = Pattern_Generator_Get_Value_Instant(190, 1257, -1);
        assert(fabsf(value - at_quarter[i]) < 0.001f);
        Pattern_Generator_Update(2007);
        float initial = types[i] == PATTERN_SINE ? 50 : types[i] == PATTERN_SQUARE ? 100 : 0;
        assert(fabsf(Pattern_Generator_Get_Value(190, -1) - initial) < 0.001f);
    }
}

static void test_application_and_cli(void)
{
    setup(100);
    App_Gen_Init(250);
    Pattern_Config_t cfg = { .spn = 190, .pattern_type = PATTERN_CONSTANT,
                             .min_value = 1500, .max_value = 1500, .param1 = 1500,
                             .start_ms = 7, .duration_ms = 40, .timeframe_ms = 20 };
    assert(App_Gen_Config_Pattern(&cfg, NULL) == APP_CFG_OK);
    App_Gen_Start(0, J1939_SCHED_MODE_SMOOTH);
    App_Gen_Process(106); assert(frames == 0);
    App_Gen_Process(107); assert(frames == 1);
    App_Gen_Process(127); assert(frames == 2);
    clock_ms = 147;
    App_Gen_Process(clock_ms);
    assert(!App_Gen_Is_Running() && frames == 2);
    assert(strstr(console, "$END,147,") && strstr(console, "Queued: 2"));
    serial_input = "SCENARIO\n";
    App_Cli_Poll();
    assert(App_Gen_Is_Running());
    assert(J1939_Sched_Get_Signal_Count() == sizeof(k_scenario_signals) / sizeof(k_scenario_signals[0]));
    assert(strstr(console, k_scenario_id));
    serial_input = "STOP\n";
    App_Cli_Poll();
    unsigned stopped_frames = frames;
    App_Gen_Process(200);
    assert(!App_Gen_Is_Running() && frames == stopped_frames);
    clock_ms = 200;
    serial_input = "START\n";
    App_Cli_Poll();
    assert(App_Gen_Is_Running());
    App_Gen_Clear();
    App_Gen_Start(0, J1939_SCHED_MODE_SMOOTH);
    assert(!App_Gen_Is_Running());

    /* Legacy CONFIG keeps its seconds-based wire syntax, including fractions. */
    serial_input = "CONFIG 190 0 0 3000 1500 20 0.007 0.040\n";
    App_Cli_Poll();
    const J1939_Sched_Signal_t *sig = J1939_Sched_Get_Signal(0);
    assert(sig && sig->start_ms == 7 && sig->duration_ms == 40);
    App_Signal_Request_t bad = { .spn = 190, .t_start = NAN };
    assert(App_Gen_Config_Signal(&bad, NULL) == APP_CFG_INVALID);
    cfg.max_value = NAN;
    assert(App_Gen_Config_Pattern(&cfg, NULL) == APP_CFG_INVALID);
}

int main(void)
{
    test_active_windows(); test_shared_fields(); test_deadlines_and_errors();
    test_inactive_gap(); test_wrap_and_long_run(); test_patterns();
    test_application_and_cli();
    puts("Firmware pattern/scheduler tests passed");
    return 0;
}
