/*
 * =============================================================================
 * FILE : j1939_pgn_timing.c
 * WHAT : Per-PGN transmit profile table.
 * =============================================================================
 */

#include "j1939_pgn_timing.h"
#include "j1939_signal_definitions.h"
#include <stddef.h>

static const J1939_PGN_Timing_t k_pgn_timing[] = {
    /* pgn                   smooth  sae  stress prio  source address */
    { PGN_EEC1,               10u,   20u,   5u,  3u, J1939_SA_ENGINE_1       },
    { PGN_EEC2,               15u,   50u,  10u,  3u, J1939_SA_ENGINE_1       },
    { PGN_ERC1,               15u,   50u,  10u,  3u, J1939_SA_ENGINE_1       },
    { PGN_VDS,                15u,   50u,  10u,  3u, J1939_SA_ENGINE_1       },
    { PGN_ETC2,               15u,   50u,  10u,  3u, J1939_SA_TRANSMISSION_1 },
    { PGN_EBC1,               20u,  100u,  20u,  6u, J1939_SA_BRAKES         },
    { PGN_CCVS1,              20u,  100u,  20u,  6u, J1939_SA_ENGINE_1       },
    { PGN_LFE,                20u,  100u,  20u,  6u, J1939_SA_ENGINE_1       },
    { PGN_AT1_SCR_DOSING,     20u,  100u,  20u,  6u, J1939_SA_ENGINE_1       },
    { PGN_TURBO1,             20u,  500u,  50u,  6u, J1939_SA_ENGINE_1       },
    { PGN_ENGINE_FLUIDS1,     20u,  500u,  50u,  6u, J1939_SA_ENGINE_1       },
    { PGN_ENGINE_FLUIDS2,     20u,  500u,  50u,  6u, J1939_SA_ENGINE_1       },
    { PGN_TRANS_FLUIDS1,      20u, 1000u, 100u,  6u, J1939_SA_TRANSMISSION_1 },
    { PGN_BRAKE_AIR_PRESS,    20u, 1000u, 100u,  6u, J1939_SA_BRAKES         },
};

static const J1939_PGN_Timing_t k_pgn_timing_default =
    { 0u,                     25u, 1000u, 100u,  6u, J1939_SA_ENGINE_1       };

void J1939_PGN_Get_Timing(uint32_t pgn, J1939_PGN_Timing_t* out)
{
    if (out == NULL) {
        return;
    }

    *out = k_pgn_timing_default;
    for (size_t i = 0; i < sizeof(k_pgn_timing) / sizeof(k_pgn_timing[0]); i++) {
        if (k_pgn_timing[i].pgn == pgn) {
            *out = k_pgn_timing[i];
            break;
        }
    }
    out->pgn = pgn;
}
