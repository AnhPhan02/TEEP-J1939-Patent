/*
 * =============================================================================
 * FILE : j1939_pgn_timing.h
 * WHAT : Per-PGN transmit profile: broadcast periods, priority, source address.
 * =============================================================================
 */

#ifndef J1939_PGN_TIMING_H
#define J1939_PGN_TIMING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Source addresses (SAE J1939 preferred addresses) */
#define J1939_SA_ENGINE_1       0x00u
#define J1939_SA_TRANSMISSION_1 0x03u
#define J1939_SA_BRAKES         0x0Bu

typedef struct {
    uint32_t pgn;
    uint16_t smooth_ms;         /* High-fidelity waveform period (generator mode 0) */
    uint16_t sae_ms;            /* SAE J1939-71 nominal broadcast period (mode 1) */
    uint16_t stress_ms;         /* Bus-load stress period (mode 2) */
    uint8_t  priority;
    uint8_t  source_address;
} J1939_PGN_Timing_t;

/* Fill transmit profile for a PGN; unknown PGNs get a 1 s / prio 6 default */
void J1939_PGN_Get_Timing(uint32_t pgn, J1939_PGN_Timing_t* out);

#ifdef __cplusplus
}
#endif
#endif /* J1939_PGN_TIMING_H */
