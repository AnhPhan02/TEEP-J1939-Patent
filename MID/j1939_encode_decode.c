/*
 * =============================================================================
 * FILE : j1939_encode_decode.c
 * WHAT : Bit-level encode and decode for J1939 signal payloads.
 *        Uses hybrid approach: fast direct extraction for aligned signals,
 *        bit-by-bit fallback for unaligned signals.
 * =============================================================================
 */

#include "j1939_encode_decode.h"
#include <math.h>
#include <string.h>

/* =============================================================================
 * PRIVATE: Clamp float to [lo, hi]
 * ============================================================================= */
static float prv_clamp_value(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

bool J1939_Is_Signal_Scalar_In_Buffer(
    const J1939_Signal_Definition_t* signal_def,
    uint16_t payload_len)
{
    if ((signal_def == NULL) ||
        (payload_len == 0u) ||
        (signal_def->start_byte < 1u) ||
        (signal_def->start_bit < 1u) || (signal_def->start_bit > 8u) ||
        (signal_def->num_bits < 1u) || (signal_def->num_bits > 32u)) {
        return false;
    }

    uint32_t start = ((uint32_t)signal_def->start_byte - 1u) * 8u +
                     ((uint32_t)signal_def->start_bit - 1u);
    uint32_t buffer_bits = (uint32_t)payload_len * 8u;
    return (start < buffer_bits) &&
           ((uint32_t)signal_def->num_bits <= (buffer_bits - start));
}

bool J1939_Is_Signal_8Byte_Scalar(const J1939_Signal_Definition_t* signal_def)
{
    return J1939_Is_Signal_Scalar_In_Buffer(signal_def, 8u);
}

/* =============================================================================
 * PRIVATE: Convert 1-based (byte, bit) to a 0-based absolute bit index
 * ============================================================================= */
static uint32_t prv_get_absolute_start_bit(const J1939_Signal_Definition_t* signal_def)
{
    uint32_t byte_0based = (uint32_t)signal_def->start_byte - 1u;
    uint32_t bit_0based  = (uint32_t)signal_def->start_bit - 1u;
    return byte_0based * 8u + bit_0based;
}

/* =============================================================================
 * PRIVATE: Check if signal is perfectly byte-aligned (starts at bit 1)
 * ============================================================================= */
static bool prv_is_byte_aligned(const J1939_Signal_Definition_t* signal_def)
{
    return (signal_def->start_bit == 1u);
}

/* =============================================================================
 * FAST PATH: Direct extraction for byte-aligned signals
 * Works for signals that start at bit 1 (LSB of a byte)
 * ============================================================================= */
static uint32_t prv_extract_aligned_bits(const uint8_t* payload,
                                          const J1939_Signal_Definition_t* signal_def)
{
    uint32_t byte_idx = (uint32_t)signal_def->start_byte - 1u;
    uint8_t num_bits = (uint8_t)signal_def->num_bits;
    
    switch (num_bits) {
        case 8:
            return (uint32_t)payload[byte_idx];
            
        case 16:
            return (uint32_t)payload[byte_idx] | 
                   ((uint32_t)payload[byte_idx + 1] << 8);
            
        case 24:
            return (uint32_t)payload[byte_idx] | 
                   ((uint32_t)payload[byte_idx + 1] << 8) |
                   ((uint32_t)payload[byte_idx + 2] << 16);
            
        case 32:
            return (uint32_t)payload[byte_idx] | 
                   ((uint32_t)payload[byte_idx + 1] << 8) |
                   ((uint32_t)payload[byte_idx + 2] << 16) |
                   ((uint32_t)payload[byte_idx + 3] << 24);
            
        default:
            /* Fall through to slow path for other sizes */
            break;
    }
    
    /* For sizes not covered above (like 4, 12 bits), use slow path */
    return 0;
}

/* =============================================================================
 * SLOW PATH: Bit-by-bit extraction for unaligned or non-standard size signals
 * Works for ANY bit alignment and ANY length (1-32 bits)
 * ============================================================================= */
static uint32_t prv_extract_unaligned_bits(const uint8_t* payload,
                                            uint32_t abs_start_bit,
                                            uint8_t num_bits)
{
    uint32_t result = 0u;
    
    for (uint8_t i = 0u; i < num_bits; i++) {
        uint32_t absolute_bit = abs_start_bit + i;
        uint32_t byte_idx     = absolute_bit / 8u;
        uint8_t bit_in_byte   = (uint8_t)(absolute_bit % 8u);
        
        if (payload[byte_idx] & (1u << bit_in_byte)) {
            result |= (1u << i);
        }
    }
    
    return result;
}

/* =============================================================================
 * HYBRID EXTRACTION: Chooses fastest method based on signal alignment
 * ============================================================================= */
static uint32_t prv_extract_raw_value(const uint8_t* payload,
                                       const J1939_Signal_Definition_t* signal_def)
{
    /* Fast path exists only for complete 8/16/24/32-bit little-endian fields. */
    if (prv_is_byte_aligned(signal_def) &&
        ((signal_def->num_bits == 8u) ||
         (signal_def->num_bits == 16u) ||
         (signal_def->num_bits == 24u) ||
         (signal_def->num_bits == 32u))) {
        return prv_extract_aligned_bits(payload, signal_def);
    }
    
    /* Slow path: Unaligned or small signals (bit-by-bit) */
    uint32_t abs_start_bit = prv_get_absolute_start_bit(signal_def);
    return prv_extract_unaligned_bits(payload, abs_start_bit,
                                      (uint8_t)signal_def->num_bits);
}

/* =============================================================================
 * FAST PATH: Direct write for byte-aligned signals
 * ============================================================================= */
static void prv_write_aligned_bits(uint8_t* payload,
                                    const J1939_Signal_Definition_t* signal_def,
                                    uint32_t value)
{
    uint32_t byte_idx = (uint32_t)signal_def->start_byte - 1u;
    uint8_t num_bits = (uint8_t)signal_def->num_bits;
    
    switch (num_bits) {
        case 8:
            payload[byte_idx] = (uint8_t)(value & 0xFF);
            break;
            
        case 16:
            payload[byte_idx]     = (uint8_t)(value & 0xFF);
            payload[byte_idx + 1] = (uint8_t)((value >> 8) & 0xFF);
            break;
            
        case 24:
            payload[byte_idx]     = (uint8_t)(value & 0xFF);
            payload[byte_idx + 1] = (uint8_t)((value >> 8) & 0xFF);
            payload[byte_idx + 2] = (uint8_t)((value >> 16) & 0xFF);
            break;
            
        case 32:
            payload[byte_idx]     = (uint8_t)(value & 0xFF);
            payload[byte_idx + 1] = (uint8_t)((value >> 8) & 0xFF);
            payload[byte_idx + 2] = (uint8_t)((value >> 16) & 0xFF);
            payload[byte_idx + 3] = (uint8_t)((value >> 24) & 0xFF);
            break;
            
        default:
            /* Fall through - handled by caller */
            break;
    }
}

/* =============================================================================
 * SLOW PATH: Bit-by-bit write for unaligned signals
 * ============================================================================= */
static void prv_write_unaligned_bits(uint8_t* payload,
                                      uint32_t abs_start_bit,
                                      uint8_t num_bits,
                                      uint32_t value)
{
    for (uint8_t i = 0u; i < num_bits; i++) {
        uint32_t absolute_bit = abs_start_bit + i;
        uint32_t byte_idx     = absolute_bit / 8u;
        uint8_t bit_in_byte   = (uint8_t)(absolute_bit % 8u);
        
        if (value & (1u << i)) {
            payload[byte_idx] |=  (uint8_t)(1u << bit_in_byte);
        } else {
            payload[byte_idx] &= (uint8_t)(~(1u << bit_in_byte));
        }
    }
}

/* =============================================================================
 * HYBRID WRITE: Chooses fastest method based on signal alignment
 * ============================================================================= */
static void prv_write_raw_value(uint8_t* payload,
                                 const J1939_Signal_Definition_t* signal_def,
                                 uint32_t value)
{
    /* Fast path: Byte-aligned signals */
    if (prv_is_byte_aligned(signal_def) && 
        (signal_def->num_bits == 8 || signal_def->num_bits == 16 || 
         signal_def->num_bits == 24 || signal_def->num_bits == 32)) {
        prv_write_aligned_bits(payload, signal_def, value);
        return;
    }
    
    /* Slow path: Unaligned or small signals */
    uint32_t abs_start_bit = prv_get_absolute_start_bit(signal_def);
    prv_write_unaligned_bits(payload, abs_start_bit,
                             (uint8_t)signal_def->num_bits, value);
}

/* =============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================= */

/* =============================================================================
 * J1939_Decode_Signal
 * ============================================================================= */
bool J1939_Decode_Signal_Sized(
    const J1939_Signal_Definition_t* signal_def,
    const uint8_t* payload,
    uint16_t payload_len,
    float* out_physical_value)
{
    if ((payload == NULL) || (out_physical_value == NULL) ||
        !J1939_Is_Signal_Scalar_In_Buffer(signal_def, payload_len)) {
        return false;
    }
    
    /* Step 1: Extract raw integer from payload bits */
    uint32_t raw_value = prv_extract_raw_value(payload, signal_def);
    
    /* Step 2: Apply J1939 scaling formula: physical = (raw * resolution) + offset */
    *out_physical_value = ((float)raw_value * signal_def->resolution) +
                          signal_def->offset;
    return true;
}

float J1939_Decode_Signal(const J1939_Signal_Definition_t* signal_def,
                          const uint8_t* payload)
{
    float physical_value = 0.0f;
    (void)J1939_Decode_Signal_Sized(signal_def, payload, 8u,
                                    &physical_value);
    return physical_value;
}

/* =============================================================================
 * J1939_Encode_Signal
 * ============================================================================= */
bool J1939_Encode_Signal_Sized(
    const J1939_Signal_Definition_t* signal_def,
    float physical_value,
    uint8_t* payload,
    uint16_t payload_len)
{
    if ((payload == NULL) ||
        !J1939_Is_Signal_Scalar_In_Buffer(signal_def, payload_len) ||
        (signal_def->resolution == 0.0f)) {
        return false;
    }
    
    /* Step 1: Clamp to valid range */
    float clamped_value = prv_clamp_value(physical_value,
                                          signal_def->min_physical,
                                          signal_def->max_physical);
    
    /* Step 2: Convert physical to raw using inverse formula */
    /* raw = round((physical - offset) / resolution) */
    float raw_float = (clamped_value - signal_def->offset) / signal_def->resolution;
    if (raw_float < 0.0f) {
        raw_float = 0.0f;
    }
    uint32_t raw_int = (uint32_t)(raw_float + 0.5f);
    
    /* Step 3: Ensure raw value fits in available bits */
    uint32_t max_raw_value = (signal_def->num_bits >= 32u) ? 0xFFFFFFFFu : (((uint32_t)1u << signal_def->num_bits) - 1u);
    if (raw_int > max_raw_value) {
        raw_int = max_raw_value;
    }
    
    /* Step 4: Write raw bits to payload */
    prv_write_raw_value(payload, signal_def, raw_int);
    
    return true;
}

bool J1939_Encode_Signal(const J1939_Signal_Definition_t* signal_def,
                         float physical_value,
                         uint8_t* payload)
{
    return J1939_Encode_Signal_Sized(signal_def, physical_value, payload, 8u);
}

/* =============================================================================
 * J1939_Is_Value_Valid
 * ============================================================================= */
bool J1939_Is_Value_Valid(const J1939_Signal_Definition_t* signal_def, 
                          float physical_value)
{
    if (signal_def == NULL) {
        return false;
    }
    return (physical_value >= signal_def->min_physical) &&
           (physical_value <= signal_def->max_physical);
}
