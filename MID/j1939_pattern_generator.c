/*
 * =============================================================================
 * FILE : j1939_pattern_generator.c
 * WHAT : Implementation of signal pattern generator
 * =============================================================================
 */

#include "j1939_pattern_generator.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* =============================================================================
 * INTERNAL RUNTIME STATE
 * ============================================================================= */
typedef struct {
    Pattern_Config_t config;
    float   current_value;
    uint32_t last_update_ms;
    float   ramp_direction;
    uint32_t step_state_index;
} Pattern_State_t;

static Pattern_State_t  g_patterns[PATTERN_GEN_MAX_SIGNALS];
static uint8_t          g_pattern_count = 0u;
static uint32_t g_last_time_ms = 0u;
static uint64_t g_elapsed_ms = 0u;
static bool             g_is_running = true;

/* =============================================================================
 * PRIVATE: Fast 32-bit PRNG for pure stochastic random signals
 * ============================================================================= */
static inline uint32_t prv_xorshift32(uint32_t* state)
{
    uint32_t x = *state;
    if (x == 0u) x = 0xDEADBEEFu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* =============================================================================
 * PRIVATE: Clamp value to range
 * ============================================================================= */
static float prv_clamp(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}



/* =============================================================================
 * PRIVATE: Compute next value for a pattern
 * ============================================================================= */
static float prv_compute_next_value(Pattern_State_t* state, uint32_t current_time_ms)
{
    if (!g_is_running) {
        return 0.0f; /* Return 0 after stopping */
    }

    const Pattern_Config_t* cfg = &state->config;
    /* Accumulate unsigned tick differences so long continuous runs survive wrap. */
    uint64_t elapsed_ms = g_elapsed_ms + (uint32_t)(current_time_ms - g_last_time_ms);
    if (elapsed_ms < cfg->start_ms ||
        (cfg->duration_ms > 0u && elapsed_ms - cfg->start_ms >= cfg->duration_ms)) {
        return 0.0f; /* Scheduler leaves inactive fields unassigned instead of encoding this. */
    }
    uint64_t active_ms = elapsed_ms - cfg->start_ms;
    float range = cfg->max_value - cfg->min_value;
    float seconds = cfg->period_seconds > 0.0f ? cfg->period_seconds :
                    (cfg->ramp_period_seconds > 0.0f ? cfg->ramp_period_seconds :
                    (cfg->sine_period_seconds > 0.0f ? cfg->sine_period_seconds : 10.0f));
    uint32_t period_ms = cfg->waveform_period_ms;
    if (period_ms == 0u) {
        period_ms = seconds >= 2147483.0f ? 2147483000u : (uint32_t)(seconds * 1000.0f);
        if (period_ms == 0u) period_ms = 1u;
    }
    /* Reduce integer time before converting to float to preserve phase precision. */
    float phase = (float)(active_ms % period_ms) / (float)period_ms;

    switch (cfg->pattern_type) {
        
        case PATTERN_CONSTANT:
            /* Pure constant value specified by user (param1) or initial_value */
            if (cfg->param1 >= cfg->min_value && cfg->param1 <= cfg->max_value) {
                return cfg->param1;
            }
            return cfg->initial_value;
            
        case PATTERN_RAMP: {
            /* Pure linear sawtooth sweep: sweeps from min to max linearly, then loops */
            return cfg->min_value + phase * range;
        }
        
        case PATTERN_SINE: {
            /* Continuous smooth harmonic sinusoidal wave oscillating between min and max */
            float center = (cfg->max_value + cfg->min_value) * 0.5f;
            float amplitude = range * 0.5f;
            float val = center + amplitude * sinf(2.0f * M_PI * phase);
            return prv_clamp(val, cfg->min_value, cfg->max_value);
        }

        case PATTERN_TRIANGLE: {
            /* Symmetric linear triangle sweep: sweeps min -> max -> min */
            float u = (phase < 0.5f) ? (phase * 2.0f) : (2.0f * (1.0f - phase));
            return cfg->min_value + u * range;
        }
        
        case PATTERN_STEP: {
            /* Discrete staircase climbing min -> max through N distinct horizontal plateaus */
            int num_steps = (cfg->param1 >= 2.0f && cfg->param1 <= 50.0f) ? (int)cfg->param1 : 8;
            int step_idx = (int)(phase * (float)num_steps);
            if (step_idx >= num_steps) step_idx = num_steps - 1;
            float step_u = (float)step_idx / (float)(num_steps - 1);
            return cfg->min_value + step_u * range;
        }

        case PATTERN_SQUARE: {
            /* Clean rectangular square wave: HIGH for first 50%, LOW for second 50% */
            return (phase < 0.5f) ? cfg->max_value : cfg->min_value;
        }
        
        case PATTERN_RANDOM_WALK: {
            /* Pure non-repeating stochastic random signal (not a harmonic pattern) */
            uint32_t prng_seed = (cfg->spn * 2654435761u) ^ (current_time_ms * 1664525u + 1013904223u);
            uint32_t r1 = prv_xorshift32(&prng_seed);
            float u = (float)(r1 % 1000000u) / 1000000.0f;
            
            if (cfg->param1 > 1.0f && cfg->param1 <= 100.0f) {
                float step_size = (range / cfg->param1);
                float delta = (u * 2.0f - 1.0f) * step_size;
                float new_val = state->current_value + delta;
                return prv_clamp(new_val, cfg->min_value, cfg->max_value);
            } else {
                return cfg->min_value + u * range;
            }
        }
        
        case PATTERN_STATE_SEQUENCE: {
            if ((cfg->state_values == NULL) || (cfg->num_states == 0u)) {
                return (phase < 0.5f) ? cfg->max_value : cfg->min_value;
            }
            uint32_t hold = (cfg->state_hold_ms > 0u) ? cfg->state_hold_ms : 1000u;
            uint32_t idx = (active_ms / hold) % (uint32_t)cfg->num_states;
            return cfg->state_values[idx];
        }
        
        default:
            return cfg->min_value;
    }
}

/* =============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================= */

void Pattern_Generator_Init(uint32_t sim_start_time_ms)
{
    memset(g_patterns, 0, sizeof(g_patterns));
    g_pattern_count = 0u;
    g_last_time_ms = sim_start_time_ms;
    g_elapsed_ms = 0u;
    g_is_running = true;
}

bool Pattern_Generator_Register(const Pattern_Config_t* config)
{
    if (config == NULL) return false;

    /* If SPN is already registered, update it in place immediately! */
    for (uint8_t i = 0u; i < g_pattern_count; i++) {
        if (g_patterns[i].config.spn == config->spn) {
            g_patterns[i].config = *config;
            g_patterns[i].current_value = config->initial_value;
            g_patterns[i].last_update_ms = g_last_time_ms;
            g_patterns[i].ramp_direction = +1.0f;
            g_patterns[i].step_state_index = 0u;
            return true;
        }
    }
    
    if (g_pattern_count >= PATTERN_GEN_MAX_SIGNALS) return false;
    
    Pattern_State_t* state = &g_patterns[g_pattern_count];
    state->config = *config;
    state->current_value = config->initial_value;
    state->last_update_ms = g_last_time_ms;
    state->ramp_direction = +1.0f;
    state->step_state_index = 0u;
    
    g_pattern_count++;
    return true;
}

void Pattern_Generator_Update(uint32_t current_time_ms)
{
    if (!g_is_running) return;
    g_elapsed_ms += (uint32_t)(current_time_ms - g_last_time_ms);
    g_last_time_ms = current_time_ms;

    for (uint8_t i = 0u; i < g_pattern_count; i++) {
        Pattern_State_t* state = &g_patterns[i];
        state->current_value = prv_compute_next_value(state, current_time_ms);
        state->last_update_ms = current_time_ms;
    }
}

float Pattern_Generator_Get_Value(uint32_t spn, float default_value)
{
    for (uint8_t i = 0u; i < g_pattern_count; i++) {
        if (g_patterns[i].config.spn == spn) {
            return g_patterns[i].current_value;
        }
    }
    return default_value;
}

float Pattern_Generator_Get_Value_Instant(uint32_t spn, uint32_t current_time_ms, float default_value)
{
    for (uint8_t i = 0u; i < g_pattern_count; i++) {
        if (g_patterns[i].config.spn == spn) {
            return prv_compute_next_value(&g_patterns[i], current_time_ms);
        }
    }
    return default_value;
}

void Pattern_Generator_Reset(uint32_t current_time_ms)
{
    g_last_time_ms = current_time_ms;
    g_elapsed_ms = 0u;
    g_is_running = true;
    
    for (uint8_t i = 0u; i < g_pattern_count; i++) {
        Pattern_State_t* state = &g_patterns[i];
        state->current_value = state->config.initial_value;
        state->last_update_ms = current_time_ms;
        state->ramp_direction = +1.0f;
    }
}

void Pattern_Generator_Stop(void)
{
    g_is_running = false;
    for (uint8_t i = 0u; i < g_pattern_count; i++) {
        Pattern_State_t* state = &g_patterns[i];
        state->current_value = 0.0f;
    }
}

uint8_t Pattern_Generator_Get_Count(void)
{
    return g_pattern_count;
}

bool Pattern_Generator_Is_Running(void)
{
    return g_is_running;
}