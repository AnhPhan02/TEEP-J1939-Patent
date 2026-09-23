/*
 * =============================================================================
 * FILE : j1939_signal_definitions.h
 * WHAT : J1939 PGN and SPN definitions with encoding properties.
 *        All values from SAE J1939-71 Application Layer standard.
 * =============================================================================
 */

#ifndef J1939_SIGNAL_DEFINITIONS_H
#define J1939_SIGNAL_DEFINITIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* =============================================================================
 * PGN NUMBERS (18-bit values embedded in 29-bit extended CAN ID)
 * ============================================================================= */

/* Engine control */
#define PGN_EEC1            61444u  /* 0xF004 Electronic Engine Controller 1   20 ms */
#define PGN_EEC2            61442u  /* 0xF002 Electronic Engine Controller 2   50 ms */
#define PGN_ENGINE_TEMP1    65262u  /* 0xFEEE Engine Temperature 1           1000 ms */
#define PGN_ENGINE_FLUIDS1  65263u  /* 0xFEEF Engine Fluid Level/Pressure     500 ms */
#define PGN_ENGINE_FLUIDS2  65270u  /* 0xFEF6 Engine Fluids 2                 500 ms */

/* Vehicle motion */
#define PGN_CCVS1           65265u  /* 0xFEF1 Cruise Control/Vehicle Speed 1  100 ms */
#define PGN_VDS             61449u  /* 0xF009 Vehicle Dynamic Stability         50 ms */

/* Transmission */
#define PGN_ETC2            61445u  /* 0xF005 Electronic Transmission Ctrl 2   50 ms */
#define PGN_TRANS_FLUIDS1   65272u  /* 0xFEF8 Transmission Fluids 1          1000 ms */

/* Brakes */
#define PGN_EBC1            61441u  /* 0xF001 Electronic Brake Controller 1   100 ms */
#define PGN_ERC1            61440u  /* 0xF000 Electronic Retarder Controller   50 ms */
#define PGN_BRAKE_AIR_PRESS 65274u  /* 0xFEFA Service Brake Air Pressure     1000 ms */

/* Aftertreatment */
#define PGN_AT1_SCR_DOSING  61475u  /* 0xF023 AT1 SCR Dosing Rate             100 ms */
#define PGN_AT1_SCR_TANK    65110u  /* 0xFE56 AT1 SCR Tank Info              1000 ms */
#define PGN_DPF_TEMP1       64947u  /* 0xFDB3 AT1 DPF Temperatures          1000 ms */

/* Environment / ambient */
#define PGN_AMB             65269u  /* 0xFEF5 Ambient Conditions             1000 ms */

/* Electrical */
#define PGN_VOLTS_AMPS      65271u  /* 0xFEF7 Electrical                     1000 ms */

/* Fan */
#define PGN_FAN_DRIVE       65213u  /* 0xFEBD Fan Drive                      1000 ms */

/* Fuel / dash */
#define PGN_DASH            65276u  /* 0xFEFC Dash Display (fuel level etc.) 1000 ms */

/* Time */
#define PGN_TIME_DATE       65254u  /* 0xFEE6 Time / Date                    1000 ms */

/* Turbo */
#define PGN_TURBO1          65190u  /* 0xFEA6 Turbocharger 1                  500 ms */

/* Water in fuel */
#define PGN_WATER_IN_FUEL   65279u  /* 0xFEFF Water In Fuel Indicator        1000 ms */
#define PGN_LFE             65266u  /* 0xFEF2 Fuel Economy Liquid            100 ms */


/* =============================================================================
 * SPN NUMBERS (unique signal identifiers inside PGN payloads)
 * ============================================================================= */

/* ---- EEC1 (PGN 61444) ---- */
#define SPN_ENGINE_TORQUE_MODE      899u
#define SPN_DRIVER_DEMAND_TORQUE    512u
#define SPN_ACTUAL_ENGINE_TORQUE    513u
#define SPN_ENGINE_SPEED            190u
#define SPN_ENGINE_DEMAND_TORQUE    2432u

/* ---- EEC2 (PGN 61443) ---- */
#define SPN_ACCEL_PEDAL_POS         91u
#define SPN_ENGINE_LOAD_PCT         92u

/* ---- Engine Temperature 1 (PGN 65262) ---- */
#define SPN_COOLANT_TEMP            110u
#define SPN_ENGINE_OIL_TEMP         175u

/* ---- Engine Fluids 1 (PGN 65263) ---- */
#define SPN_ENGINE_OIL_LEVEL        98u
#define SPN_ENGINE_OIL_PRESSURE     100u

/* ---- CCVS1 (PGN 65265) ---- */
#define SPN_VEHICLE_SPEED           84u

/* ---- VDS (PGN 61449) ---- */
#define SPN_STEERING_ANGLE          1807u
#define SPN_YAW_RATE                1808u
#define SPN_LATERAL_ACCEL           1809u
#define SPN_LONG_ACCEL              1810u

/* ---- ETC2 (PGN 61445) ---- */
#define SPN_TRANS_GEAR              523u

/* ---- Transmission Fluids 1 (PGN 65272) ---- */
#define SPN_TRANS_OIL_PRESSURE      127u
#define SPN_TRANS_OIL_TEMP          177u

/* ---- EBC1 (PGN 61441) ---- */
#define SPN_BRAKE_PEDAL_POS         521u

/* ---- Brake Air Pressure (PGN 65274) ---- */
#define SPN_BRAKE_APP_PRESSURE      116u

/* ---- ERC1 Retarder (PGN 61440) ---- */
#define SPN_RETARDER_TORQUE         520u

/* ---- AT1 SCR Dosing (PGN 61475) ---- */
#define SPN_SCR_DOSING_RATE         4331u

/* ---- AT1 SCR Tank (PGN 65110) ---- */
#define SPN_SCR_TANK_LEVEL          1761u
#define SPN_SCR_TANK_TEMP           3031u

/* ---- DPF Temperature 1 (PGN 64947) ---- */
#define SPN_DPF_INLET_TEMP          3250u
#define SPN_DPF_OUTLET_TEMP         3246u

/* ---- Ambient (PGN 65269) ---- */
#define SPN_BARO_PRESSURE           108u
#define SPN_CAB_TEMP                170u
#define SPN_AMBIENT_TEMP            171u
#define SPN_AIR_INLET_TEMP          172u
#define SPN_ROAD_SURFACE_TEMP       79u

/* ---- Electrical (PGN 65271) ---- */
#define SPN_ALTERNATOR_CURRENT      115u
#define SPN_BATTERY_VOLTAGE         167u
#define SPN_BATTERY_POTENTIAL       168u

/* ---- Fan Drive (PGN 65213) ---- */
#define SPN_FAN_DRIVE_PCT           1639u
#define SPN_FAN_SPEED               975u

/* ---- Dash / Fuel (PGN 65276) ---- */
#define SPN_WASHER_FLUID_LEVEL      80u
#define SPN_FUEL_LEVEL              96u

/* ---- Time/Date (PGN 65254) ---- */
#define SPN_SECONDS                 959u
#define SPN_MINUTES                 960u
#define SPN_HOURS                   961u
#define SPN_MONTH                   963u
#define SPN_DAY                     962u
#define SPN_YEAR                    964u

/* ---- Turbocharger 1 (PGN 65190) ---- */
#define SPN_TURBO_BOOST_PRESS       1127u
#define SPN_TURBO_SPEED             103u

/* ---- Water In Fuel (PGN 65279) ---- */
#define SPN_WATER_IN_FUEL_FLAG      97u

/* ---- Engine Fluids 2 / Intake Pressure (PGN 65270 / 0xFEF6) ---- */
#define SPN_INTAKE_MANIFOLD_PRESSURE 102u

/* ---- Fuel Economy Liquid (PGN 65266 / 0xFEF2) ---- */
#define SPN_ENGINE_FUEL_RATE        183u
#define SPN_ENGINE_INST_FUEL_ECON   184u
#define SPN_ENGINE_AVG_FUEL_ECON    185u


/* =============================================================================
 * SIGNAL PROPERTIES STRUCTURE
 * Contains everything needed to encode/decode one SPN
 * ============================================================================= */
typedef struct {
    uint32_t    pgn;              /* PGN this signal belongs to */
    uint32_t    spn;              /* Unique SPN identifier */
    const char* name;             /* Human-readable signal name */
    const char* unit;             /* Engineering unit (e.g., "rpm", "degC") */
    uint8_t     start_byte;       /* 1-based byte position in 8-byte payload (1..8) */
    uint8_t     start_bit;        /* 1-based bit inside start_byte (1=LSB, 8=MSB) */
    uint8_t     num_bits;         /* Width of signal in bits (1..32) */
    float       resolution;       /* Physical units per raw LSB count */
    float       offset;           /* Added after scaling: physical = raw*res + offset */
    float       min_physical;     /* Minimum valid physical value (from J1939) */
    float       max_physical;     /* Maximum valid physical value (from J1939) */
} J1939_Signal_Definition_t;


/* =============================================================================
 * DATABASE ACCESS FUNCTIONS
 * ============================================================================= */
extern const J1939_Signal_Definition_t J1939_Signal_Database[];
extern const uint16_t                  J1939_Signal_Count;

const J1939_Signal_Definition_t* J1939_Find_Signal_By_SPN(uint32_t spn_number);

#ifdef __cplusplus
}
#endif
#endif /* J1939_SIGNAL_DEFINITIONS_H */
