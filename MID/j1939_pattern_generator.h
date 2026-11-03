/*
 * =============================================================================
 * FILE : j1939_pattern_generator.h
 * WHAT : Signal pattern generator for simulating realistic vehicle data
 * =============================================================================
 */

#ifndef J1939_PATTERN_GENERATOR_H
#define J1939_PATTERN_GENERATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_build_profile.h"
#include "j1939_signal_definitions.h"

/* Pool capacity J1939_WAVEFORM_POOL_CAPACITY is selected by
 * j1939_build_profile.h; only waveform (non-constant) signals use it. */

/* =============================================================================
 * PATTERN TYPE ENUM
 * ============================================================================= */
typedef enum {
    PATTERN_CONSTANT        = 0,  /* Constant DC target value */
    PATTERN_RAMP            = 1,  /* Linear sawtooth sweep (min -> max -> reset) */
    PATTERN_SINE            = 2,  /* Continuous smooth harmonic sinusoidal wave */
    PATTERN_TRIANGLE        = 3,  /* Continuous symmetric linear sweep (min -> max -> min) */
    PATTERN_STEP            = 4,  /* Distinct discrete staircase levels */
    PATTERN_SQUARE          = 5,  /* Square / pulse wave (50% duty cycle min <-> max) */
    PATTERN_RANDOM_WALK     = 6,  /* Continuous bounded stochastic sensor noise / drift */
    PATTERN_STATE_SEQUENCE  = 7   /* Multi-state cyclic sequence */
} Pattern_Type_t;

/* =============================================================================
 * PATTERN CONFIGURATION STRUCT
 * ============================================================================= */
typedef struct {
    uint16_t catalog_index;                  /* Unique generated catalog record */
    Pattern_Type_t  pattern_type;           /* Type of pattern */
    
    /* Range limits (apply to all patterns) */
    float   min_value;
    float   max_value;
    float   initial_value;
    
    /* Timing & Parameters */
    float   period_seconds;                 /* Primary period (sec) for Ramp, Sine, Step, Random */
    float   param1;                         /* User parameter / constant target */
    
    /* PATTERN_RAMP */
    float   ramp_period_seconds;            /* Time to sweep min->max */
    
    /* PATTERN_SINE */
    float   sine_period_seconds;            /* Full oscillation period */
    
    /* PATTERN_STEP */
    float    step_size;                     /* Increment per step */
    uint32_t step_interval_ms;              /* Milliseconds between steps */
    
    /* PATTERN_RANDOM_WALK */
    float   random_max_step;                /* Max delta per update */
    
    /* PATTERN_STATE_SEQUENCE */
    const float* state_values;              /* Array of discrete values */
    uint8_t      num_states;                /* Length of array */
    uint32_t     state_hold_ms;             /* Time per state */
    
    /* Timeframe Scheduling */
    uint32_t timeframe_ms;                  /* Broadcast cycle timeframe in milliseconds (e.g., 10ms, 20ms, 50ms, 100ms) */
    float    t_start_sec;                   /* Active window start time in seconds (0 = immediate) */
    float    t_duration_sec;                /* Active window duration in seconds (0 = continuous) */
    
    /* Timing */
    uint32_t update_interval_ms;            /* How often to recalculate */
    
} Pattern_Config_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */
#if ENABLE_WAVEFORM

/* Initialize the generator system */
void Pattern_Generator_Init(uint32_t sim_start_time_ms);

/* Register a new pattern */
bool Pattern_Generator_Register(const Pattern_Config_t* config);

/* Read-only admission check used to keep APP registration atomic. */
bool Pattern_Generator_Can_Register(uint16_t catalog_index);

/* Remove a signal from the waveform pool; false if it was not registered. */
bool Pattern_Generator_Unregister(uint16_t catalog_index);
bool Pattern_Generator_Is_Registered(uint16_t catalog_index);

/* Enumerate the compact catalog identities in the waveform pool. */
bool Pattern_Generator_Get_Catalog_Index(uint8_t ordinal,
                                         uint16_t* out_catalog_index);

/* Update all registered patterns (call every 1ms from main loop) */
void Pattern_Generator_Update(uint32_t current_time_ms);

/* Get current value for one unique catalog signal. */
float Pattern_Generator_Get_Value(uint16_t catalog_index,
                                  float default_value);

/* Get instantaneous mathematically continuous value for one catalog signal. */
float Pattern_Generator_Get_Value_Instant(uint16_t catalog_index,
                                          uint32_t current_time_ms,
                                          float default_value);

/* Reset all patterns to initial values */
void Pattern_Generator_Reset(uint32_t current_time_ms);

/* Stop pattern generation and reset values to 0 */
void Pattern_Generator_Stop(void);

/* Get number of registered patterns */
uint8_t Pattern_Generator_Get_Count(void);

/* Check if any pattern is currently active */
bool Pattern_Generator_Is_Running(void);

#else /* !ENABLE_WAVEFORM: constant-only build, empty waveform pool */

static inline void Pattern_Generator_Init(uint32_t sim_start_time_ms)
{ (void)sim_start_time_ms; }
static inline bool Pattern_Generator_Register(const Pattern_Config_t* config)
{ (void)config; return false; }
static inline bool Pattern_Generator_Can_Register(uint16_t catalog_index)
{ (void)catalog_index; return false; }
static inline bool Pattern_Generator_Unregister(uint16_t catalog_index)
{ (void)catalog_index; return false; }
static inline bool Pattern_Generator_Is_Registered(uint16_t catalog_index)
{ (void)catalog_index; return false; }
static inline bool Pattern_Generator_Get_Catalog_Index(uint8_t ordinal,
                                                       uint16_t* out_catalog_index)
{ (void)ordinal; (void)out_catalog_index; return false; }
static inline void Pattern_Generator_Update(uint32_t current_time_ms)
{ (void)current_time_ms; }
static inline float Pattern_Generator_Get_Value(uint16_t catalog_index,
                                                float default_value)
{ (void)catalog_index; return default_value; }
static inline float Pattern_Generator_Get_Value_Instant(uint16_t catalog_index,
                                                        uint32_t current_time_ms,
                                                        float default_value)
{ (void)catalog_index; (void)current_time_ms; return default_value; }
static inline void Pattern_Generator_Reset(uint32_t current_time_ms)
{ (void)current_time_ms; }
static inline void Pattern_Generator_Stop(void) {}
static inline uint8_t Pattern_Generator_Get_Count(void) { return 0u; }
static inline bool Pattern_Generator_Is_Running(void) { return false; }

#endif /* ENABLE_WAVEFORM */

#ifdef __cplusplus
}
#endif
#endif /* J1939_PATTERN_GENERATOR_H */
