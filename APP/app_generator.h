/*
 * =============================================================================
 * FILE : app_generator.h
 * WHAT : Generator state machine: configure signals, start/stop runs,
 *        drive the TX scheduler, telemetry and status LED.
 * =============================================================================
 */

#ifndef APP_GENERATOR_H
#define APP_GENERATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_pattern_generator.h"
#include "j1939_tx_scheduler.h"
#include "j1939_customer_profiles.h"

typedef struct {
    uint32_t       pgn;             /* 0 = SPN-only; accepted only when unique */
    uint32_t       spn;
    Pattern_Type_t type;
    float          min_value;
    float          max_value;
    float          param1;          /* Period (s) or constant value */
    uint32_t       timeframe_ms;    /* 0 = automatic */
    float          t_start;
    float          t_dur;
} App_Signal_Request_t;

typedef enum {
    APP_CFG_OK = 0,
    APP_CFG_UNKNOWN_SPN,
    APP_CFG_AMBIGUOUS_SPN,
    APP_CFG_UNSUPPORTED_SIGNAL,
    APP_CFG_TABLE_FULL
} App_Config_Status_t;

typedef enum {
    APP_SET_CONST_OK = 0,
    APP_SET_CONST_UNKNOWN_KEY,
    APP_SET_CONST_VALUE_OUT_OF_RANGE,
    APP_SET_CONST_UNSUPPORTED_SIGNAL,
    APP_SET_CONST_RESOURCE_FULL,
    APP_SET_CONST_TRANSACTION_OPEN
} App_Set_Const_Status_t;

/* Phase 4A full-profile transaction. The active profile is never modified by
 * BEGIN/SET; only a successful COMMIT swaps the value bank and rebuilds the
 * scheduler. A rejected transaction must be aborted or committed (which
 * reports REJECTED and discards it) before another transaction can begin. */
typedef enum {
    APP_CONFIG_TXN_OK = 0,
    APP_CONFIG_TXN_INVALID_STATE,
    APP_CONFIG_TXN_ALREADY_OPEN,
    APP_CONFIG_TXN_NOT_OPEN,
    APP_CONFIG_TXN_REJECTED,
    APP_CONFIG_TXN_UNKNOWN_KEY,
    APP_CONFIG_TXN_VALUE_OUT_OF_RANGE,
    APP_CONFIG_TXN_UNSUPPORTED_SIGNAL,
    APP_CONFIG_TXN_RESOURCE_FULL,
    APP_CONFIG_TXN_COUNT_MISMATCH,
    APP_CONFIG_TXN_INTERNAL_ERROR,
    APP_CONFIG_TXN_BLOB_INCOMPLETE,     /* commit: blob bytes missing */
    APP_CONFIG_TXN_BAD_ARGUMENT,        /* malformed chunk/padding */
    APP_CONFIG_TXN_UNKNOWN_PROFILE,
    APP_CONFIG_TXN_PROFILE_KEY_NOT_ALLOWED,
    APP_CONFIG_TXN_PROFILE_MISMATCH
} App_Config_Transaction_Status_t;

/* Maximum blob payload bytes carried by one WRITE_BLOB chunk. */
#define APP_BLOB_CHUNK_BYTES  3u

/* Global percentage is in basis points: 0..10000 = 0.00..100.00 %. */
#define APP_GLOBAL_PERCENT_MAX_BP  10000u

void App_Gen_Init(uint32_t baud_kbps);
bool App_Gen_Set_Baud(uint32_t baud_kbps);
uint32_t App_Gen_Get_Baud(void);

App_Config_Status_t App_Gen_Config_Signal(const App_Signal_Request_t* req,
                                          uint16_t* out_catalog_index);
App_Set_Const_Status_t App_Gen_Set_Constant_Raw(uint16_t signal_key,
                                                uint32_t raw_value,
                                                uint32_t* applied_raw_value);

App_Config_Transaction_Status_t App_Gen_Begin_Config(
    uint16_t expected_scalar_count,
    uint8_t expected_blob_count);
/* Profile-aware Phase 5 entry point. CUSTOM keeps the Phase 4 engineer-mode
 * behavior; a generated profile ID enforces its exact key mask and counts. */
App_Config_Transaction_Status_t App_Gen_Begin_Profile_Config(
    uint16_t expected_scalar_count,
    uint8_t expected_blob_count,
    uint8_t profile_id);
App_Config_Transaction_Status_t App_Gen_Stage_Constant_Raw(
    uint16_t signal_key,
    uint32_t raw_value,
    uint16_t* out_staged_count);
/* Stage one blob chunk. chunk holds APP_BLOB_CHUNK_BYTES bytes; only the
 * first min(3, total_length - offset) are payload and the rest must be zero.
 * out_written_bytes receives the distinct blob bytes staged so far. */
App_Config_Transaction_Status_t App_Gen_Stage_Blob_Chunk(
    uint16_t signal_key,
    uint8_t total_length,
    uint8_t offset,
    const uint8_t chunk[APP_BLOB_CHUNK_BYTES],
    uint16_t* out_written_bytes);
App_Config_Transaction_Status_t App_Gen_Commit_Config(
    uint16_t* out_generation,
    uint16_t* out_signal_count,
    uint8_t* out_pgn_count);
void App_Gen_Abort_Config(void);
bool App_Gen_Config_Transaction_Open(void);
bool App_Gen_Config_Transaction_Rejected(void);

/* Map every active constant record whose catalog marks it global_control to
 * raw = raw_min + floor((bp * (raw_max - raw_min) + 5000) / 10000) and commit
 * them in one generation. Boolean/enum/blob/waveform records are excluded and
 * left unchanged. Allowed while running; rejected while a transaction is open.
 * With no affected record nothing is committed (generation unchanged). */
App_Config_Transaction_Status_t App_Gen_Set_Global_Percent(
    uint16_t percentage_bp,
    uint16_t* out_generation,
    uint16_t* out_affected_count,
    uint16_t* out_excluded_count);
/* Pure mapping used by App_Gen_Set_Global_Percent (exposed for UI parity tests). */
uint32_t App_Gen_Global_Percent_To_Raw(uint32_t raw_min, uint32_t raw_max,
                                       uint16_t percentage_bp);
uint32_t App_Gen_Get_Config_Crc(void);
uint8_t App_Gen_Get_Active_Profile_Id(void);

bool App_Gen_Has_Configured_Signals(void);
bool App_Gen_Is_Signal_Configured(uint16_t signal_key);
uint16_t App_Gen_Get_Config_Generation(void);
uint16_t App_Gen_Get_Active_Signal_Count(void);
uint8_t App_Gen_Get_Active_PGN_Count(void);
bool App_Gen_Active_Profile_Can_Start(void);
void App_Gen_Load_Defaults(void);
void App_Gen_Clear(void);

void App_Gen_Start(uint32_t duration_sec, J1939_Sched_Mode_t mode);
void App_Gen_Stop(void);
bool App_Gen_Is_Running(void);
uint32_t App_Gen_Get_Frames_Sent(void);

void App_Gen_Print_Status(void);
void App_Gen_Print_Can_Timing(void);

/* Call every loop iteration */
void App_Gen_Process(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* APP_GENERATOR_H */
