#include "j1939_can_helper.h"
#include "j1939_encode_decode.h"
#include "j1939_signal_definitions.h"
#include "j1939_pattern_generator.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* =============================================================================
   J1939 DYNAMIC SERIAL & WAVEFORM GENERATOR FIRMWARE
   Creators: Vishal Meyyappan R (3rd Year ECE - ACT) & Srikar (4th year ECE)
   Institution: Chennai Institute of Technology (CIT Chennai) & STUST
   ============================================================================= */

#define MAX_ACTIVE_SIGNALS 65
#define MAX_ACTIVE_PGNS    35

typedef struct {
    uint32_t spn;
    uint32_t pgn;
    const J1939_Signal_Definition_t* def;
    uint32_t timeframe_ms;
    float    t_start;
    float    t_dur;
} Active_Signal_t;

typedef struct {
    uint32_t pgn;
    uint32_t period_smooth_ms;
    uint32_t period_sae_ms;
    uint32_t period_stress_ms;
    uint32_t period_custom_ms;
    uint32_t last_tx_ms;
    uint8_t  priority;
    uint8_t  source_address;
} Active_PGN_t;

static Active_Signal_t g_active_signals[MAX_ACTIVE_SIGNALS];
static uint8_t         g_active_signal_count = 0;

static Active_PGN_t    g_active_pgns[MAX_ACTIVE_PGNS];
static uint8_t         g_active_pgn_count = 0;

/* Generator Operating State */
enum RunMode {
    MODE_SMOOTH_WAVEFORM = 0,  // High-Fidelity Waveform Generation (15ms - 25ms per PGN -> silky smooth graphs in TSMaster!)
    MODE_SAE_STANDARD    = 1,  // Strict SAE J1939 Broadcast Timings (20ms - 1000ms periodic)
    MODE_STRESS_TEST     = 2   // Maximum Bus Load Stress Testing (5ms - 20ms burst)
};

static bool     g_is_running = false;
static uint8_t  g_run_mode = MODE_SMOOTH_WAVEFORM;
static uint32_t g_test_start_ms = 0;
static uint32_t g_test_duration_ms = 0; // 0 = continuous
static uint32_t g_total_frames_sent = 0;
static uint32_t g_last_telemetry_ms = 0;
static uint32_t g_current_baud_kbps = 500;

/* Serial command buffer */
#define SERIAL_BUF_LEN 128
static char     g_serial_buf[SERIAL_BUF_LEN];
static uint8_t  g_serial_pos = 0;

/* =============================================================================
   HELPER: Get standard SAE, Smooth, and Stress period for a PGN
   ============================================================================= */
static void Get_PGN_Timing_Defaults(uint32_t pgn, uint32_t* smooth_ms, uint32_t* sae_ms, uint32_t* stress_ms, uint8_t* prio, uint8_t* sa) {
    *prio = 6;
    *sa = 0x00;
    *smooth_ms = 20; // 50 Hz high-fidelity update for silky smooth graphs in TSMaster!

    switch (pgn) {
        case PGN_EEC1:  // 61444
            *smooth_ms = 10;
            *sae_ms = 20;
            *stress_ms = 5;
            *prio = 3;
            *sa = 0x00;
            break;
        case PGN_EEC2:  // 61442
        case PGN_ERC1:  // 61440
        case PGN_VDS:   // 61449
            *smooth_ms = 15;
            *sae_ms = 50;
            *stress_ms = 10;
            *prio = 3;
            *sa = 0x00;
            break;
        case PGN_ETC2:  // 61445
            *smooth_ms = 15;
            *sae_ms = 50;
            *stress_ms = 10;
            *prio = 3;
            *sa = 0x03; // Transmission Controller
            break;
        case PGN_EBC1:  // 61441
            *smooth_ms = 20;
            *sae_ms = 100;
            *stress_ms = 20;
            *prio = 6;
            *sa = 0x0B; // Brake Controller
            break;
        case PGN_CCVS1: // 65265
        case PGN_LFE:   // 65266
        case PGN_AT1_SCR_DOSING: // 61475
            *smooth_ms = 20;
            *sae_ms = 100;
            *stress_ms = 20;
            *prio = 6;
            *sa = 0x00;
            break;
        case PGN_TURBO1:         // 65190
        case PGN_ENGINE_FLUIDS1: // 65263
        case PGN_ENGINE_FLUIDS2: // 65270
            *smooth_ms = 20;
            *sae_ms = 500;
            *stress_ms = 50;
            *prio = 6;
            *sa = 0x00;
            break;
        case PGN_TRANS_FLUIDS1:  // 65272
            *smooth_ms = 20;
            *sae_ms = 1000;
            *stress_ms = 100;
            *prio = 6;
            *sa = 0x03; // Transmission Controller
            break;
        case PGN_BRAKE_AIR_PRESS: // 65274
            *smooth_ms = 20;
            *sae_ms = 1000;
            *stress_ms = 100;
            *prio = 6;
            *sa = 0x0B; // Brake Controller
            break;
        default:
            *smooth_ms = 25;
            *sae_ms = 1000;
            *stress_ms = 100;
            *prio = 6;
            *sa = 0x00;
            break;
    }
}

/* =============================================================================
   HELPER: Ensure PGN is tracked in active PGNs list
   ============================================================================= */
static void Ensure_PGN_Tracked(uint32_t pgn) {
    for (uint8_t i = 0; i < g_active_pgn_count; i++) {
        if (g_active_pgns[i].pgn == pgn) {
            return;
        }
    }
    if (g_active_pgn_count < MAX_ACTIVE_PGNS) {
        Active_PGN_t* entry = &g_active_pgns[g_active_pgn_count++];
        entry->pgn = pgn;
        Get_PGN_Timing_Defaults(pgn, &entry->period_smooth_ms, &entry->period_sae_ms, &entry->period_stress_ms, &entry->priority, &entry->source_address);
        entry->period_custom_ms = 0;
        entry->last_tx_ms = millis();
    }
}

/* =============================================================================
   COMMAND: CLEAR ALL PATTERNS
   ============================================================================= */
static void Command_Clear(void) {
    Pattern_Generator_Init(millis());
    g_active_signal_count = 0;
    g_active_pgn_count = 0;
    g_is_running = false;
    Serial.println("[ACK] CLEAR OK - All patterns and active PGNs reset.");
}

/* =============================================================================
   COMMAND: CONFIG SPN
   Format: CONFIG <spn> <pattern_type> <min> <max> <param1> [timeframe_ms] [t_start] [t_dur]
   pattern_type: 0=Constant, 1=Ramp, 2=Sine, 3=Triangle, 4=Step, 5=Square, 6=RandomWalk, 7=StateSeq
   ============================================================================= */
static void Command_Config(char* args) {
    if (args == NULL) {
        Serial.println("[ERR] CONFIG invalid syntax. Expected: CONFIG <spn> <type> <min> <max> [param1] [timeframe_ms] [t_start] [t_dur]");
        return;
    }

    char* token = strtok(args, " \t");
    if (!token) {
        Serial.println("[ERR] CONFIG missing SPN");
        return;
    }
    uint32_t spn = (uint32_t)strtoul(token, NULL, 10);

    token = strtok(NULL, " \t");
    if (!token) {
        Serial.println("[ERR] CONFIG missing type");
        return;
    }
    int type = atoi(token);

    token = strtok(NULL, " \t");
    float min_val = token ? (float)atof(token) : 0.0f;

    token = strtok(NULL, " \t");
    float max_val = token ? (float)atof(token) : 100.0f;

    token = strtok(NULL, " \t");
    float param1 = token ? (float)atof(token) : 10.0f;

    token = strtok(NULL, " \t");
    uint32_t timeframe_ms = token ? (uint32_t)strtoul(token, NULL, 10) : 0;

    token = strtok(NULL, " \t");
    float t_start = token ? (float)atof(token) : 0.0f;

    token = strtok(NULL, " \t");
    float t_dur = token ? (float)atof(token) : 0.0f;

    const J1939_Signal_Definition_t* def = J1939_Find_Signal_By_SPN(spn);
    if (def == NULL) {
        Serial.print("[ERR] SPN ");
        Serial.print(spn);
        Serial.println(" not found in J1939 signal database!");
        return;
    }

    Pattern_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.spn = spn;
    cfg.pattern_type = (Pattern_Type_t)type;
    cfg.min_value = min_val;
    cfg.max_value = max_val;
    cfg.param1 = param1;
    cfg.timeframe_ms = timeframe_ms;
    cfg.t_start_sec = t_start;
    cfg.t_duration_sec = t_dur;
    cfg.update_interval_ms = (timeframe_ms > 0 && timeframe_ms < 10) ? timeframe_ms : 10;

    float period = (param1 > 0.1f) ? param1 : 10.0f;
    cfg.period_seconds = period;
    cfg.ramp_period_seconds = period;
    cfg.sine_period_seconds = period;

    switch (cfg.pattern_type) {
        case PATTERN_CONSTANT:
            /* If user provided value within range, use it; otherwise default to midpoint or min */
            if (param1 >= min_val && param1 <= max_val) {
                cfg.initial_value = param1;
            } else {
                cfg.initial_value = (min_val + max_val) * 0.5f;
            }
            break;

        case PATTERN_STEP:
            cfg.step_size = (max_val - min_val) / 8.0f;
            cfg.step_interval_ms = (uint32_t)(period * 1000.0f / 16.0f);
            cfg.initial_value = min_val;
            break;

        case PATTERN_RANDOM_WALK:
            cfg.initial_value = (min_val + max_val) * 0.5f;
            cfg.random_max_step = (max_val - min_val) * 0.05f;
            break;

        case PATTERN_SINE:
            cfg.initial_value = (min_val + max_val) * 0.5f;
            break;

        case PATTERN_RAMP:
        case PATTERN_TRIANGLE:
        case PATTERN_SQUARE:
        default:
            cfg.initial_value = min_val;
            break;
    }

    if (!Pattern_Generator_Register(&cfg)) {
        Serial.println("[ERR] Failed to register pattern (maximum signals reached).");
        return;
    }

    // Add to active signal list if not already present
    bool found = false;
    for (uint8_t i = 0; i < g_active_signal_count; i++) {
        if (g_active_signals[i].spn == spn) {
            g_active_signals[i].def = def;
            g_active_signals[i].pgn = def->pgn;
            g_active_signals[i].timeframe_ms = timeframe_ms;
            g_active_signals[i].t_start = t_start;
            g_active_signals[i].t_dur = t_dur;
            found = true;
            break;
        }
    }
    if (!found && g_active_signal_count < MAX_ACTIVE_SIGNALS) {
        g_active_signals[g_active_signal_count].spn = spn;
        g_active_signals[g_active_signal_count].pgn = def->pgn;
        g_active_signals[g_active_signal_count].def = def;
        g_active_signals[g_active_signal_count].timeframe_ms = timeframe_ms;
        g_active_signals[g_active_signal_count].t_start = t_start;
        g_active_signals[g_active_signal_count].t_dur = t_dur;
        g_active_signal_count++;
    }

    Ensure_PGN_Tracked(def->pgn);
    if (timeframe_ms > 0) {
        for (uint8_t i = 0; i < g_active_pgn_count; i++) {
            if (g_active_pgns[i].pgn == def->pgn) {
                // If timeframe is faster or custom, update PGN cycle time
                if (g_active_pgns[i].period_custom_ms == 0 || timeframe_ms < g_active_pgns[i].period_custom_ms) {
                    g_active_pgns[i].period_custom_ms = timeframe_ms;
                }
                break;
            }
        }
    }

    Serial.print("[ACK] CONFIG SPN ");
    Serial.print(spn);
    Serial.print(" (");
    Serial.print(def->name);
    Serial.print(") -> Type: ");
    const char* type_names[] = {"Constant", "Ramp (Sawtooth)", "Sine Wave", "Triangle", "Step Function", "Square Wave", "Random Walk", "State Sequence"};
    if (type >= 0 && type <= 7) Serial.print(type_names[type]);
    else Serial.print(type);
    Serial.print(" | Range: [");
    Serial.print(min_val);
    Serial.print(" .. ");
    Serial.print(max_val);
    Serial.print(" ");
    Serial.print(def->unit);
    Serial.print("] | Timeframe: ");
    if (timeframe_ms > 0) {
        Serial.print(timeframe_ms);
        Serial.print(" ms");
    } else {
        Serial.print("Auto");
    }
    if (t_dur > 0.0f) {
        Serial.print(" [Active: ");
        Serial.print(t_start, 1);
        Serial.print("s - ");
        Serial.print(t_start + t_dur, 1);
        Serial.print("s]");
    }
    Serial.println();
}

/* Helper to load default signals */
static void Load_Default_Signals(void) {
    Pattern_Generator_Init(millis());
    g_active_signal_count = 0;
    g_active_pgn_count = 0;

    // SPN 190: Engine Speed (Sine wave 800 - 3500 rpm)
    const J1939_Signal_Definition_t* def_190 = J1939_Find_Signal_By_SPN(SPN_ENGINE_SPEED);
    if (def_190) {
        Pattern_Config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.spn = SPN_ENGINE_SPEED;
        cfg.pattern_type = PATTERN_SINE;
        cfg.min_value = 800.0f;
        cfg.max_value = 3500.0f;
        cfg.initial_value = 800.0f;
        cfg.sine_period_seconds = 8.0f;
        cfg.update_interval_ms = 10;
        Pattern_Generator_Register(&cfg);

        g_active_signals[g_active_signal_count].spn = SPN_ENGINE_SPEED;
        g_active_signals[g_active_signal_count].pgn = PGN_EEC1;
        g_active_signals[g_active_signal_count].def = def_190;
        g_active_signal_count++;
        Ensure_PGN_Tracked(PGN_EEC1);
    }

    // SPN 91: Accelerator Pedal (Ramp 0 - 100%)
    const J1939_Signal_Definition_t* def_91 = J1939_Find_Signal_By_SPN(SPN_ACCEL_PEDAL_POS);
    if (def_91) {
        Pattern_Config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.spn = SPN_ACCEL_PEDAL_POS;
        cfg.pattern_type = PATTERN_RAMP;
        cfg.min_value = 0.0f;
        cfg.max_value = 100.0f;
        cfg.initial_value = 0.0f;
        cfg.ramp_period_seconds = 10.0f;
        cfg.update_interval_ms = 10;
        Pattern_Generator_Register(&cfg);

        g_active_signals[g_active_signal_count].spn = SPN_ACCEL_PEDAL_POS;
        g_active_signals[g_active_signal_count].pgn = def_91->pgn;
        g_active_signals[g_active_signal_count].def = def_91;
        g_active_signal_count++;
        Ensure_PGN_Tracked(def_91->pgn);
    }

    // SPN 84: Vehicle Speed (Ramp 0 - 120 km/h)
    const J1939_Signal_Definition_t* def_84 = J1939_Find_Signal_By_SPN(SPN_VEHICLE_SPEED);
    if (def_84) {
        Pattern_Config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.spn = SPN_VEHICLE_SPEED;
        cfg.pattern_type = PATTERN_RAMP;
        cfg.min_value = 0.0f;
        cfg.max_value = 120.0f;
        cfg.initial_value = 0.0f;
        cfg.ramp_period_seconds = 15.0f;
        cfg.update_interval_ms = 10;
        Pattern_Generator_Register(&cfg);

        g_active_signals[g_active_signal_count].spn = SPN_VEHICLE_SPEED;
        g_active_signals[g_active_signal_count].pgn = PGN_CCVS1;
        g_active_signals[g_active_signal_count].def = def_84;
        g_active_signal_count++;
        Ensure_PGN_Tracked(PGN_CCVS1);
    }
}

/* =============================================================================
   CORE FUNCTION: START GENERATOR
   duration_sec: 0 = continuous
   mode: 0 = High-Fidelity Smooth Waveform (50 Hz), 1 = SAE Standards, 2 = Stress Testing
   ============================================================================= */
static void Start_Generator(uint32_t duration_sec, int mode) {
    if (mode == 1) {
        g_run_mode = MODE_SAE_STANDARD;
    } else if (mode == 2) {
        g_run_mode = MODE_STRESS_TEST;
    } else {
        g_run_mode = MODE_SMOOTH_WAVEFORM; // Mode 0: High-fidelity continuous smooth waveforms!
    }
    g_test_start_ms = millis();
    g_test_duration_ms = duration_sec * 1000;
    g_is_running = true;
    g_total_frames_sent = 0;

    // Reset PGN last tx timers
    uint32_t now = millis();
    for (uint8_t i = 0; i < g_active_pgn_count; i++) {
        g_active_pgns[i].last_tx_ms = now;
    }
    Pattern_Generator_Reset(now);

    Serial.println("=========================================");
    Serial.print("[ACK] START OK | Mode: ");
    if (g_run_mode == MODE_SMOOTH_WAVEFORM) Serial.print("HIGH-FIDELITY SMOOTH WAVEFORM (50 Hz)");
    else if (g_run_mode == MODE_STRESS_TEST) Serial.print("STRESS TESTING (100 Hz)");
    else Serial.print("SAE STANDARDS COMPLIANT");
    Serial.print(" | Duration: ");
    if (g_test_duration_ms == 0) Serial.println("CONTINUOUS (No Timeout)");
    else {
        Serial.print(duration_sec);
        Serial.println(" seconds");
    }
    Serial.print("[ACK] Active Signals: ");
    Serial.print(g_active_signal_count);
    Serial.print(" | Active PGNs: ");
    Serial.println(g_active_pgn_count);
    Serial.println("=========================================");
}

/* =============================================================================
   COMMAND: START GENERATOR (Serial Command Wrapper)
   Format: START <duration_sec> <mode>
   ============================================================================= */
static void Command_Start(char* args) {
    uint32_t duration_sec = 0;
    int mode = 0;
    if (args != NULL && strlen(args) > 0) {
        char* token = strtok(args, " \t");
        if (token) duration_sec = (uint32_t)strtoul(token, NULL, 10);
        token = strtok(NULL, " \t");
        if (token) mode = atoi(token);
    }
    Start_Generator(duration_sec, mode);
}

/* =============================================================================
   COMMAND: STOP GENERATOR
   ============================================================================= */
static void Command_Stop(void) {
    g_is_running = false;
    Pattern_Generator_Stop();
    Serial.println("[ACK] STOP OK - Transmission halted (all signals returned to 0).");
    Serial.print("[STATS] Total frames transmitted: ");
    Serial.println(g_total_frames_sent);
}

/* =============================================================================
   COMMAND: BAUD SWITCH
   Format: BAUD <250|500>
   ============================================================================= */
static void Command_Baud(char* args) {
    uint32_t baud = 500;
    if (args != NULL) {
        sscanf(args, "%lu", &baud);
    }
    if (baud != 250 && baud != 500) {
        Serial.println("[ERR] Supported baud rates are 250 or 500 kbps.");
        return;
    }

    g_current_baud_kbps = baud;
    if (J1939_CAN_Hardware_Init_Baud(baud)) {
        Serial.print("[ACK] BAUD OK - CAN hardware switched to ");
        Serial.print(baud);
        Serial.println(" kbps.");
    } else {
        Serial.println("[ERR] CAN hardware failed to initialize at requested baud rate.");
    }
}

/* =============================================================================
   COMMAND: STATUS REPORT
   ============================================================================= */
static void Command_Status(void) {
    Serial.println("----------------- STATUS -----------------");
    Serial.print("Running: ");
    Serial.println(g_is_running ? "YES" : "NO (IDLE)");
    Serial.print("Mode: ");
    Serial.println((g_run_mode == MODE_STRESS_TEST) ? "STRESS TESTING" : "SAE STANDARDS");
    Serial.print("CAN Bitrate: ");
    Serial.print(g_current_baud_kbps);
    Serial.println(" kbps");
    Serial.print("Active Registered SPNs: ");
    Serial.println(g_active_signal_count);
    for (uint8_t i = 0; i < g_active_signal_count; i++) {
        Serial.print("  #");
        Serial.print(i + 1);
        Serial.print(": SPN ");
        Serial.print(g_active_signals[i].spn);
        Serial.print(" (");
        Serial.print(g_active_signals[i].def->name);
        Serial.print(") in PGN ");
        Serial.print(g_active_signals[i].pgn);
        Serial.print(" | Cur Val: ");
        Serial.print(Pattern_Generator_Get_Value(g_active_signals[i].spn, 0.0f));
        Serial.print(" ");
        Serial.println(g_active_signals[i].def->unit);
    }
    Serial.print("Total Frames Sent: ");
    Serial.println(g_total_frames_sent);
    Serial.println("------------------------------------------");
}

/* =============================================================================
   PARSE SERIAL COMMAND LINE
   ============================================================================= */
static void Process_Serial_Command(char* cmd_line) {
    while (*cmd_line == ' ' || *cmd_line == '\t') cmd_line++;
    if (*cmd_line == '\0') return;

    char* space_ptr = strchr(cmd_line, ' ');
    char* args = NULL;
    if (space_ptr != NULL) {
        *space_ptr = '\0';
        args = space_ptr + 1;
        while (*args == ' ' || *args == '\t') args++;
    }

    if (strcasecmp(cmd_line, "CONFIG") == 0) {
        Command_Config(args);
    } else if (strcasecmp(cmd_line, "START") == 0) {
        Command_Start(args);
    } else if (strcasecmp(cmd_line, "STOP") == 0) {
        Command_Stop();
    } else if (strcasecmp(cmd_line, "CLEAR") == 0) {
        Command_Clear();
    } else if (strcasecmp(cmd_line, "BAUD") == 0) {
        Command_Baud(args);
    } else if (strcasecmp(cmd_line, "STATUS") == 0) {
        Command_Status();
    } else if (strcasecmp(cmd_line, "RESET") == 0) {
        Command_Clear();
        Serial.println("[ACK] System state reset to IDLE.");
    } else {
        Serial.print("[ERR] Unknown command '");
        Serial.print(cmd_line);
        Serial.println("'. Available: CONFIG, START, STOP, CLEAR, BAUD, STATUS, RESET");
    }
}

/* =============================================================================
   SETUP
   ============================================================================= */
void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n=======================================================");
    Serial.println("  J1939 DYNAMIC WAVEFORM GENERATOR & VERIFICATION HUB  ");
    Serial.println("  Chennai Institute of Technology (CIT Chennai) & STUST");
    Serial.println("  Creators: Vishal Meyyappan R (3rd Year ECE - ACT)");
    Serial.println("            Srikar (4th year ECE)");
    Serial.println("=======================================================");

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Pattern_Generator_Init(millis());

    if (J1939_CAN_Hardware_Init_Baud(g_current_baud_kbps)) {
        Serial.println("[SUCCESS] CAN Hardware Peripheral Initialized.");
    } else {
        Serial.println("[ERROR] CAN Hardware Initialization Failed!");
    }

    // Auto-start default J1939 powertrain signals immediately on boot
    Load_Default_Signals();
    Start_Generator(0, 0); // Continuous SAE compliant mode

    Serial.println("[READY] Auto-started default J1939 transmission (EEC1, EEC2, CCVS1).");
    Serial.println("        Connect Web Dashboard to customize waveforms or view live telemetry.");
    Serial.println("-------------------------------------------------------");
}

/* =============================================================================
   LOOP
   ============================================================================= */
void loop() {
    uint32_t current_time = millis();

    /* 1. Read and buffer incoming Serial commands non-blocking */
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (g_serial_pos > 0) {
                g_serial_buf[g_serial_pos] = '\0';
                Process_Serial_Command(g_serial_buf);
                g_serial_pos = 0;
            }
        } else if (g_serial_pos < SERIAL_BUF_LEN - 1) {
            g_serial_buf[g_serial_pos++] = c;
        }
    }

    /* 2. Check test duration if running */
    if (g_is_running) {
        if (g_test_duration_ms > 0 && (current_time - g_test_start_ms >= g_test_duration_ms)) {
            g_is_running = false;
            Pattern_Generator_Stop();
            Serial.println("\n[TEST] Target test duration elapsed. Transmission completed (signals at 0).");
            Serial.print("[STATS] Total frames sent: ");
            Serial.println(g_total_frames_sent);
        }
    }

    /* 3. Execute transmission schedule if running */
    if (g_is_running && g_active_pgn_count > 0) {
        // Update all dynamic wave mathematical values
        Pattern_Generator_Update(current_time);

        // Iterate over each active PGN and transmit if period reached
        for (uint8_t i = 0; i < g_active_pgn_count; i++) {
            Active_PGN_t* pgn_entry = &g_active_pgns[i];
            uint32_t period = (pgn_entry->period_custom_ms > 0) ? pgn_entry->period_custom_ms :
                              ((g_run_mode == MODE_SMOOTH_WAVEFORM) ? pgn_entry->period_smooth_ms :
                              ((g_run_mode == MODE_STRESS_TEST) ? pgn_entry->period_stress_ms : pgn_entry->period_sae_ms));

            if (current_time - pgn_entry->last_tx_ms >= period) {
                pgn_entry->last_tx_ms = current_time;

                // 8-byte payload initialized to 0xFF (J1939 standard default for unassigned signals)
                uint8_t payload[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

                // Encode all registered signals belonging to this PGN with instantaneous precision
                for (uint8_t s = 0; s < g_active_signal_count; s++) {
                    if (g_active_signals[s].pgn == pgn_entry->pgn) {
                        float val = Pattern_Generator_Get_Value_Instant(g_active_signals[s].spn, current_time, g_active_signals[s].def->min_physical);
                        J1939_Encode_Signal(g_active_signals[s].def, val, payload);
                    }
                }

                // Transmit CAN frame
                if (J1939_CAN_Hardware_Transmit(pgn_entry->pgn, payload, 8, pgn_entry->priority, pgn_entry->source_address)) {
                    g_total_frames_sent++;
                }
            }
        }

        // Periodic telemetry logging to Serial monitor (every 250ms, optimized output)
        if (current_time - g_last_telemetry_ms >= 250) {
            g_last_telemetry_ms = current_time;

            if (g_active_signal_count > 0) {
                Serial.print("[TX] Frames: ");
                Serial.print(g_total_frames_sent);
                
                uint8_t display_count = (g_active_signal_count <= 8) ? g_active_signal_count : 6;
                for (uint8_t i = 0; i < display_count; i++) {
                    uint32_t s_spn = g_active_signals[i].spn;
                    float s_val = Pattern_Generator_Get_Value(s_spn, 0.0f);
                    Serial.print(" | SPN ");
                    Serial.print(s_spn);
                    Serial.print(": ");
                    Serial.print(s_val, 1);
                    Serial.print(" ");
                    Serial.print(g_active_signals[i].def->unit);
                }
                if (g_active_signal_count > 8) {
                    Serial.print(" | ... [Total: ");
                    Serial.print(g_active_signal_count);
                    Serial.print(" SPNs transmitting on CAN]");
                }
                Serial.println();
            }
        }

        // Active LED pulse (5 Hz)
        digitalWrite(LED_BUILTIN, (current_time / 100) % 2);
    } else {
        // Idle gentle heartbeat LED (1 Hz)
        digitalWrite(LED_BUILTIN, (current_time / 500) % 2);
    }
}