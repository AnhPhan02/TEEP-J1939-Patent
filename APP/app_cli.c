/*
 * =============================================================================
 * FILE : app_cli.c
 * WHAT : Line-based serial command interface.
 * =============================================================================
 */

#include "app_cli.h"
#include "app_config.h"
#include "app_generator.h"
#include "j1939_link.h"
#include "hal_console.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char    s_line_buf[APP_CLI_BUF_LEN];
static uint8_t s_line_pos = 0u;

static const char* const k_pattern_names[] = {
    "Constant", "Ramp (Sawtooth)", "Sine Wave", "Triangle",
    "Step Function", "Square Wave", "Random Walk", "State Sequence"
};

/* =============================================================================
 * CONFIG <spn> <type> <min> <max> [param1] [timeframe_ms] [t_start] [t_dur]
 * type: 0=Constant 1=Ramp 2=Sine 3=Triangle 4=Step 5=Square 6=RandomWalk 7=StateSeq
 * ============================================================================= */
static void prv_cmd_config(char* args)
{
    if (args == NULL) {
        hal_console_write_line("[ERR] CONFIG invalid syntax. Expected: CONFIG <spn> <type> <min> <max> [param1] [timeframe_ms] [t_start] [t_dur]");
        return;
    }

    char* token = strtok(args, " \t");
    if (token == NULL) {
        hal_console_write_line("[ERR] CONFIG missing SPN");
        return;
    }

    App_Signal_Request_t req;
    req.spn = (uint32_t)strtoul(token, NULL, 10);

    token = strtok(NULL, " \t");
    if (token == NULL) {
        hal_console_write_line("[ERR] CONFIG missing type");
        return;
    }
    int type = atoi(token);
    req.type = (Pattern_Type_t)type;

    token = strtok(NULL, " \t");
    req.min_value = token ? (float)atof(token) : 0.0f;
    token = strtok(NULL, " \t");
    req.max_value = token ? (float)atof(token) : 100.0f;
    token = strtok(NULL, " \t");
    req.param1 = token ? (float)atof(token) : 10.0f;
    token = strtok(NULL, " \t");
    req.timeframe_ms = token ? (uint32_t)strtoul(token, NULL, 10) : 0u;
    token = strtok(NULL, " \t");
    req.t_start = token ? (float)atof(token) : 0.0f;
    token = strtok(NULL, " \t");
    req.t_dur = token ? (float)atof(token) : 0.0f;

    const J1939_Signal_Definition_t* def = NULL;
    switch (App_Gen_Config_Signal(&req, &def)) {
        case APP_CFG_UNKNOWN_SPN:
            hal_console_write("[ERR] SPN ");
            hal_console_write_u32(req.spn);
            hal_console_write_line(" not found in J1939 signal database!");
            return;
        case APP_CFG_TABLE_FULL:
            hal_console_write_line("[ERR] Failed to register pattern (maximum signals reached).");
            return;
        default:
            break;
    }

    hal_console_write("[ACK] CONFIG SPN ");
    hal_console_write_u32(req.spn);
    hal_console_write(" (");
    hal_console_write(def->name);
    hal_console_write(") -> Type: ");
    if (type >= 0 && type <= 7) {
        hal_console_write(k_pattern_names[type]);
    } else {
        hal_console_write_i32(type);
    }
    hal_console_write(" | Range: [");
    hal_console_write_float(req.min_value, 2);
    hal_console_write(" .. ");
    hal_console_write_float(req.max_value, 2);
    hal_console_write(" ");
    hal_console_write(def->unit);
    hal_console_write("] | Timeframe: ");
    if (req.timeframe_ms > 0u) {
        hal_console_write_u32(req.timeframe_ms);
        hal_console_write(" ms");
    } else {
        hal_console_write("Auto");
    }
    if (req.t_dur > 0.0f) {
        hal_console_write(" [Active: ");
        hal_console_write_float(req.t_start, 1);
        hal_console_write("s - ");
        hal_console_write_float(req.t_start + req.t_dur, 1);
        hal_console_write("s]");
    }
    hal_console_write_line(NULL);
}

/* START <duration_sec> <mode>   mode: 0=Smooth 1=SAE 2=Stress */
static void prv_cmd_start(char* args)
{
    uint32_t duration_sec = 0u;
    int mode = 0;

    if (args != NULL && *args != '\0') {
        char* token = strtok(args, " \t");
        if (token) duration_sec = (uint32_t)strtoul(token, NULL, 10);
        token = strtok(NULL, " \t");
        if (token) mode = atoi(token);
    }

    J1939_Sched_Mode_t sched_mode = J1939_SCHED_MODE_SMOOTH;
    if (mode == 1) {
        sched_mode = J1939_SCHED_MODE_SAE;
    } else if (mode == 2) {
        sched_mode = J1939_SCHED_MODE_STRESS;
    }
    if (!J1939_Sched_Get_Tx_Log()) {
        hal_console_write_line("[WARN] TXLOG is OFF; UART-to-TSMaster correlation will not be recorded.");
        hal_console_write_line("[WARN] For validation: STOP, then TXLOG 1, then START 0 0.");
    }
    App_Gen_Start(duration_sec, sched_mode);
}

static void prv_cmd_stop(void)
{
    App_Gen_Stop();
    hal_console_write_line("[ACK] STOP OK - Transmission halted (all signals returned to 0).");
    hal_console_write("[STATS] Total frames transmitted: ");
    hal_console_write_u32(App_Gen_Get_Frames_Sent());
    hal_console_write_line(NULL);
}

static void prv_cmd_clear(void)
{
    App_Gen_Clear();
    hal_console_write_line("[ACK] CLEAR OK - All patterns and active PGNs reset.");
}

/* BAUD <250|500> */
static void prv_cmd_baud(char* args)
{
    uint32_t baud = (args != NULL) ? (uint32_t)strtoul(args, NULL, 10) : APP_DEFAULT_CAN_BAUD_KBPS;
    if (baud != 250u && baud != 500u) {
        hal_console_write_line("[ERR] Supported baud rates are 250 or 500 kbps.");
        return;
    }

    if (App_Gen_Set_Baud(baud)) {
        hal_console_write("[ACK] BAUD OK - CAN hardware switched to ");
        hal_console_write_u32(baud);
        hal_console_write_line(" kbps.");
    } else {
        hal_console_write_line("[ERR] CAN hardware failed to initialize at requested baud rate.");
    }
}

/* TXLOG <0|1>   per-frame $TX log for TX/RX correlation */
static void prv_cmd_txlog(char* args)
{
    if (args != NULL && *args != '\0') {
        J1939_Sched_Set_Tx_Log(atoi(args) != 0);
    }
    hal_console_write("[ACK] TXLOG ");
    hal_console_write_line(J1939_Sched_Get_Tx_Log() ? "ON" : "OFF");
}

/* =============================================================================
 * CANTEST - prove the CAN configuration and isolate firmware vs hardware
 * ============================================================================= */
static void prv_print_level(const char* label, bool high, bool expect_high)
{
    hal_console_write(label);
    hal_console_write(high ? "HIGH" : "LOW ");
    hal_console_write_line((high == expect_high) ? "  [OK]" : "  [FAIL]");
}

static void prv_cmd_cantest(void)
{
    J1939_Link_Diag_t d;
    J1939_Link_Bit_Timing_t t;
    J1939_Link_Get_Bit_Timing(&t);
    J1939_Link_Diagnose(&d);

    uint32_t brp = (d.btr & 0x3FFu) + 1u;
    uint32_t ts1 = ((d.btr >> 16) & 0xFu) + 1u;
    uint32_t ts2 = ((d.btr >> 20) & 0x7u) + 1u;
    uint32_t div = brp * (1u + ts1 + ts2);
    uint32_t remap = (d.afio_mapr >> 13) & 0x3u;
    bool test_mode = (d.btr & 0xC0000000u) != 0u;

    hal_console_write_line("============== CANTEST ==============");
    hal_console_write_line("[1] Registers read back from the chip");
    hal_console_write("    MCR=0x");  hal_console_write_hex(d.mcr);
    hal_console_write(" MSR=0x");     hal_console_write_hex(d.msr);
    hal_console_write(" BTR=0x");     hal_console_write_hex(d.btr);
    hal_console_write(" ESR=0x");     hal_console_write_hex(d.esr);
    hal_console_write_line(NULL);

    hal_console_write("    Bitrate from BTR: ");
    hal_console_write_u32((div > 0u) ? t.pclk_hz / div : 0u);
    hal_console_write(" bps (BRP ");  hal_console_write_u32(brp);
    hal_console_write(", 1+");        hal_console_write_u32(ts1);
    hal_console_write("+");           hal_console_write_u32(ts2);
    hal_console_write_line(test_mode ? " TQ) LOOPBACK/SILENT BIT SET  [FAIL]" : " TQ, normal mode)");

    hal_console_write("    CAN_REMAP = ");
    hal_console_write_u32(remap);
    hal_console_write_line((remap == 2u) ? " -> RX=PB8 TX=PB9  [OK]" : " -> not PB8/PB9  [FAIL]");

    hal_console_write("    PB8 cfg=0x");  hal_console_write_hex(d.rx_cfg);
    hal_console_write((d.rx_cfg == 0x8u || d.rx_cfg == 0x4u) ? " (input) [OK]" : " [FAIL]");
    hal_console_write(" | PB9 cfg=0x");   hal_console_write_hex(d.tx_cfg);
    hal_console_write_line((d.tx_cfg == 0xBu) ? " (AF push-pull) [OK]" : " [FAIL]");

    hal_console_write("    MCR: ");
    hal_console_write((d.mcr & 0x1u) ? "INIT MODE [FAIL] " : "normal mode [OK] ");
    hal_console_write_line((d.mcr & (1u << 6)) ? "| ABOM on" : "| ABOM off");

    hal_console_write("    MSR.RX (live CAN_RX pin) = ");
    hal_console_write_line((d.msr & (1u << 11)) ? "1 (recessive)" : "0 (dominant)");

    hal_console_write_line("[2] Transceiver echo test (bxCAN bypassed, PB9 driven as GPIO)");
    prv_print_level("    PB9 = 1 (recessive) -> PB8 = ", d.rx_high_idle, true);
    prv_print_level("    PB9 = 0 (dominant)  -> PB8 = ", !d.rx_low_dominant, false);
    prv_print_level("    PB9 = 1 (released)  -> PB8 = ", d.rx_high_release, true);

    hal_console_write("[3] Verdict: ");
    if (!d.rx_high_idle) {
        hal_console_write_line("RX stuck LOW -> bus shorted dominant, RXD (pin 4) wiring, or transceiver fault.");
    } else if (!d.rx_low_dominant) {
        hal_console_write_line("HARDWARE: transceiver does not echo TX.");
        hal_console_write_line("    Check MCP2561 pin 8 STBY = GND, pin 3 VDD = 5 V, PB9 -> pin 1 TXD, PB8 <- pin 4 RXD.");
    } else if (remap != 2u || test_mode || d.tx_cfg != 0xBu) {
        hal_console_write_line("FIRMWARE: pin echo OK but CAN configuration is wrong (see [FAIL] above).");
    } else {
        hal_console_write_line("TX->RX path OK. If errors remain: bitrate/termination/TSMaster channel (expect LEC 3 or 1/2).");
    }
    hal_console_write_line("=====================================");

    /* Restart the controller at the current bitrate */
    (void)App_Gen_Set_Baud(App_Gen_Get_Baud());
}

static void prv_process_line(char* line)
{
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return;

    char* args = NULL;
    char* space = strchr(line, ' ');
    if (space != NULL) {
        *space = '\0';
        args = space + 1;
        while (*args == ' ' || *args == '\t') args++;
    }

    if (strcasecmp(line, "CONFIG") == 0) {
        prv_cmd_config(args);
    } else if (strcasecmp(line, "START") == 0) {
        prv_cmd_start(args);
    } else if (strcasecmp(line, "STOP") == 0) {
        prv_cmd_stop();
    } else if (strcasecmp(line, "CLEAR") == 0) {
        prv_cmd_clear();
    } else if (strcasecmp(line, "BAUD") == 0) {
        prv_cmd_baud(args);
    } else if (strcasecmp(line, "STATUS") == 0) {
        App_Gen_Print_Status();
    } else if (strcasecmp(line, "RESET") == 0) {
        prv_cmd_clear();
        hal_console_write_line("[ACK] System state reset to IDLE.");
    } else if (strcasecmp(line, "CANTEST") == 0) {
        prv_cmd_cantest();
    } else if (strcasecmp(line, "TXLOG") == 0) {
        prv_cmd_txlog(args);
    } else {
        hal_console_write("[ERR] Unknown command '");
        hal_console_write(line);
        hal_console_write_line("'. Available: CONFIG, START, STOP, CLEAR, BAUD, STATUS, RESET, CANTEST, TXLOG");
    }
}

void App_Cli_Poll(void)
{
    int c;
    while ((c = hal_console_read()) >= 0) {
        if (c == '\n' || c == '\r') {
            if (s_line_pos > 0u) {
                s_line_buf[s_line_pos] = '\0';
                prv_process_line(s_line_buf);
                s_line_pos = 0u;
            }
        } else if (s_line_pos < APP_CLI_BUF_LEN - 1u) {
            s_line_buf[s_line_pos++] = (char)c;
        }
    }
}
