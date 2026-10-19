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
#include "j1939_signal_definitions.h"
#include "j1939_pattern_generator.h"
#include "j1939_tx_scheduler.h"

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
    APP_SET_CONST_RESOURCE_FULL
} App_Set_Const_Status_t;

void App_Gen_Init(uint32_t baud_kbps);
bool App_Gen_Set_Baud(uint32_t baud_kbps);
uint32_t App_Gen_Get_Baud(void);

App_Config_Status_t App_Gen_Config_Signal(const App_Signal_Request_t* req,
                                          const J1939_Signal_Definition_t** out_def);
App_Set_Const_Status_t App_Gen_Set_Constant_Raw(J1939_Signal_Key_t signal_key,
                                                uint32_t raw_value,
                                                uint32_t* applied_raw_value);
bool App_Gen_Has_Configured_Signals(void);
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
