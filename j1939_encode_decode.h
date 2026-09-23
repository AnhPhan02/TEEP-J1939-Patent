/*
 * =============================================================================
 * FILE : j1939_encode_decode.h
 * WHAT : Encode (physical value -> raw bits) and Decode (raw bits -> physical)
 * =============================================================================
 */

#ifndef J1939_ENCODE_DECODE_H
#define J1939_ENCODE_DECODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_signal_definitions.h"

/* =============================================================================
 * J1939_Decode_Signal - Extract SPN from 8-byte payload and convert to physical
 * 
 * PARAMETERS:
 *   signal_def - Pointer to signal definition from database
 *   payload    - 8-byte CAN data field received from bus
 *
 * RETURNS:
 *   Physical value in engineering units (e.g., 1800.0 for rpm)
 *   Returns 0.0f if signal_def or payload is NULL
 * ============================================================================= */
float J1939_Decode_Signal(const J1939_Signal_Definition_t* signal_def, 
                          const uint8_t* payload);

/* =============================================================================
 * J1939_Encode_Signal - Convert physical value to raw bits in payload buffer
 *
 * IMPORTANT: Caller must initialize buffer to 0xFF before encoding first SPN.
 * 0xFF = "not available" in J1939 standard.
 *
 * PARAMETERS:
 *   signal_def     - Signal definition from database
 *   physical_value - Value to encode (e.g., 1800.0f rpm)
 *   payload        - 8-byte buffer to write bits into
 *
 * RETURNS:
 *   true  - Value encoded successfully
 *   false - signal_def or payload is NULL
 * ============================================================================= */
bool J1939_Encode_Signal(const J1939_Signal_Definition_t* signal_def,
                         float physical_value,
                         uint8_t* payload);

/* =============================================================================
 * J1939_Is_Value_Valid - Check if physical value is within valid range
 * ============================================================================= */
bool J1939_Is_Value_Valid(const J1939_Signal_Definition_t* signal_def, 
                          float physical_value);

/* =============================================================================
 * J1939_Initialize_Payload - Fill 8-byte payload with 0xFF (not available)
 * ============================================================================= */
static inline void J1939_Initialize_Payload(uint8_t* payload)
{
    for (uint8_t i = 0; i < 8; i++) {
        payload[i] = 0xFF;
    }
}

#ifdef __cplusplus
}
#endif
#endif /* J1939_ENCODE_DECODE_H */