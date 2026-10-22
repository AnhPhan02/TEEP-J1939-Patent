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
    { PGN_EBS11,              10u,   10u,   5u,  6u, J1939_SA_BRAKES         },
    { PGN_ETC1,               10u,   10u,   5u,  6u, J1939_SA_TRANSMISSION_1 },
    { PGN_VDC2,               10u,   10u,   5u,  6u, J1939_SA_BRAKES         },
    { PGN_EAC2,               20u,   20u,  10u,  6u, J1939_SA_ENGINE_1       },
    { PGN_HRW,                20u,   20u,  10u,  2u, J1939_SA_BRAKES         },
    { PGN_TCO1,               50u,   50u,  25u,  3u, J1939_SA_ENGINE_1       },
    { PGN_EEC1,               10u,   20u,   5u,  3u, J1939_SA_ENGINE_1       },
    { PGN_EEC2,               50u,   50u,  10u,  3u, J1939_SA_ENGINE_1       },
    { PGN_ERC1,              100u,  100u,  20u,  3u, J1939_SA_ENGINE_1       },
    { PGN_ETC2,               15u,   50u,  10u,  3u, J1939_SA_TRANSMISSION_1 },
    { PGN_EBC1,              100u,  100u,  20u,  6u, J1939_SA_BRAKES         },
    { PGN_FLI2,              100u,  100u,  20u,  6u, J1939_SA_ENGINE_1       },
    { PGN_PTODE,             100u,  100u,  20u,  6u, J1939_SA_ENGINE_1       },
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

typedef struct{
    uint32_t pgn;
    uint8_t priority;
}J1939_PGN_Priority_t;

static const J1939_PGN_Priority_t k_pgn_priority[] = {
    { PGN_HRW,             2u },
    { PGN_EEC1,            3u },
    { PGN_EEC2,            3u },
    { PGN_ERC1,            3u },
    { PGN_TCO1,            3u },
    { PGN_LC,              3u },
    { PGN_ACC1,            4u },
    { PGN_EBS11,           6u },
    { PGN_CM1,             6u },
    { PGN_EBC1,            6u },
    { PGN_ETC1,            6u },
    { PGN_ETC2,            6u },
    { PGN_VDS,             6u },
    { PGN_EAC2,            6u },
    { PGN_SWA,             6u },
    { PGN_AT1_SCR_DOSING,  6u },
    { PGN_HRFC,            6u },
    { PGN_SBS,             6u },
    { PGN_AUXC,            6u },
    { PGN_ET4,             6u },
    { PGN_EGF2,            6u },
    { PGN_PTODE,           6u },
    { PGN_DC2,             6u },
    { PGN_DPF_TEMP1,       6u },
    { PGN_OELC,            6u },
    { PGN_TI1,             6u },
    { PGN_FLI2,            6u },
    { PGN_TIRE2,           6u },
    { PGN_ET3,             6u },
    { PGN_CVW,             6u },
    { PGN_ET2,             6u },
    { PGN_IMT1,            6u },
    { PGN_AIR1,            6u },
    { PGN_FD,              6u },
    { PGN_EBC2,            6u },
    { PGN_VDHR,            6u },
    { PGN_EBS21,           6u },
    { PGN_DM1,             6u },
    { PGN_IO,              6u },
    { PGN_EEC3,            6u },
    { PGN_VD,              6u },
    { PGN_RC,              6u },
    { PGN_EC,              6u },
    { PGN_SHUTDOWN,        6u },
    { PGN_HOURS,           6u },
    { PGN_TD,              6u },
    { PGN_LFC,             6u },
    { PGN_VW,              6u },
    { PGN_VI,              6u },
    { PGN_ET1,             6u },
    { PGN_EFL_P1,          6u },
    { PGN_CCVS1,           6u },
    { PGN_LFE,             6u },
    { PGN_AMB,             6u },
    { PGN_IC1,             6u },
    { PGN_VEP,             6u },
    { PGN_TRANS_FLUIDS1,   6u },
    { PGN_AI,              6u },
    { PGN_B,               6u },
    { PGN_DD,              6u },
    { PGN_WATER_IN_FUEL,   6u },
    { PGN_PROPB_FB,        6u },
    { PGN_VP2,             7u },
    { PGN_EI,              7u },
};

uint8_t J1939_PGN_Get_Priority(uint32_t pgn)
{
    for(size_t i = 0u; i < sizeof(k_pgn_priority)/sizeof(k_pgn_priority[0]); i++)
    {
        if(k_pgn_priority[i].pgn == pgn)
        {
            return k_pgn_priority[i].priority;
        }
    }

    return k_pgn_timing_default.priority;
}

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
    out->priority = J1939_PGN_Get_Priority(pgn);
}
