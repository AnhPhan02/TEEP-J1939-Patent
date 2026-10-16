/*
 * =============================================================================
 * FILE : j1939_signal_definitions.h
 * WHAT : J1939 PGN/SPN metadata generated from the 179-SPN master workbook.
 * SOURCE: doc/J1939_179_SPN_Master_Specification_Table.xlsx,
 *         sheet "Master 179 SPN Database", rows 6-184.
 * =============================================================================
 */

#ifndef J1939_SIGNAL_DEFINITIONS_H
#define J1939_SIGNAL_DEFINITIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define J1939_SIGNAL_COUNT_EXPECTED       179u
#define J1939_TX_PERIOD_NOT_FIXED_MS      0u

/* Canonical PGN identifiers from the master workbook. */
#define PGN_EBS11            712u  /* 0x02C8 */
#define PGN_CM1            57344u  /* 0xE000 */
#define PGN_ERC1           61440u  /* 0xF000 */
#define PGN_EBC1           61441u  /* 0xF001 */
#define PGN_ETC1           61442u  /* 0xF002 */
#define PGN_EEC2           61443u  /* 0xF003 */
#define PGN_EEC1           61444u  /* 0xF004 */
#define PGN_ETC2           61445u  /* 0xF005 */
#define PGN_VDC2           61449u  /* 0xF009 */
#define PGN_EAC2           61451u  /* 0xF00B */
#define PGN_SWA            61469u  /* 0xF01D */
#define PGN_HRFC           64777u  /* 0xFD09 */
#define PGN_SBS            64791u  /* 0xFD17 */
#define PGN_AUXC           64850u  /* 0xFD52 */
#define PGN_ET4            64870u  /* 0xFD66 */
#define PGN_EGF2           64930u  /* 0xFDA2 */
#define PGN_PTODE          64932u  /* 0xFDA4 */
#define PGN_DC2            64933u  /* 0xFDA5 */
#define PGN_OELC           64972u  /* 0xFDCC */
#define PGN_LC             65089u  /* 0xFE41 */
#define PGN_TI1            65110u  /* 0xFE56 */
#define PGN_FLI2           65115u  /* 0xFE5B */
#define PGN_TIRE2          65118u  /* 0xFE5E */
#define PGN_ET3            65129u  /* 0xFE69 */
#define PGN_TCO1           65132u  /* 0xFE6C */
#define PGN_HRW            65134u  /* 0xFE6E */
#define PGN_ACC1           65135u  /* 0xFE6F */
#define PGN_CVW            65136u  /* 0xFE70 */
#define PGN_VP2            65165u  /* 0xFE8D */
#define PGN_EI             65170u  /* 0xFE92 */
#define PGN_ET2            65188u  /* 0xFEA4 */
#define PGN_IMT1           65190u  /* 0xFEA6 */
#define PGN_AIR1           65198u  /* 0xFEAE */
#define PGN_FD             65213u  /* 0xFEBD */
#define PGN_EBC2           65215u  /* 0xFEBF */
#define PGN_VDHR           65217u  /* 0xFEC1 */
#define PGN_EBS21          65222u  /* 0xFEC6 */
#define PGN_DM1            65226u  /* 0xFECA */
#define PGN_IO             65244u  /* 0xFEDC */
#define PGN_EEC3           65247u  /* 0xFEDF */
#define PGN_VD             65248u  /* 0xFEE0 */
#define PGN_RC             65249u  /* 0xFEE1 */
#define PGN_EC             65251u  /* 0xFEE3 */
#define PGN_SHUTDOWN       65252u  /* 0xFEE4 */
#define PGN_HOURS          65253u  /* 0xFEE5 */
#define PGN_TD             65254u  /* 0xFEE6 */
#define PGN_LFC            65257u  /* 0xFEE9 */
#define PGN_VW             65258u  /* 0xFEEA */
#define PGN_VI             65260u  /* 0xFEEC */
#define PGN_ET1            65262u  /* 0xFEEE */
#define PGN_EFL_P1         65263u  /* 0xFEEF */
#define PGN_CCVS1          65265u  /* 0xFEF1 */
#define PGN_LFE            65266u  /* 0xFEF2 */
#define PGN_AMB            65269u  /* 0xFEF5 */
#define PGN_IC1            65270u  /* 0xFEF6 */
#define PGN_VEP            65271u  /* 0xFEF7 */
#define PGN_AI             65273u  /* 0xFEF9 */
#define PGN_B              65274u  /* 0xFEFA */
#define PGN_DD             65276u  /* 0xFEFC */
#define PGN_PROPB_FB       65457u  /* 0xFFB1 */

/* Backward-compatible PGN aliases used by the existing firmware modules. */
#define PGN_ENGINE_TEMP1       PGN_ET1
#define PGN_ENGINE_FLUIDS1     PGN_EFL_P1
#define PGN_ENGINE_FLUIDS2     PGN_IC1
#define PGN_VDS                PGN_VDC2
#define PGN_TRANS_FLUIDS1      65272u
#define PGN_BRAKE_AIR_PRESS    PGN_B
#define PGN_AT1_SCR_DOSING     61475u
#define PGN_AT1_SCR_TANK       PGN_TI1
#define PGN_DPF_TEMP1          64947u
#define PGN_VOLTS_AMPS         PGN_VEP
#define PGN_FAN_DRIVE          PGN_FD
#define PGN_DASH               PGN_DD
#define PGN_TIME_DATE          PGN_TD
#define PGN_TURBO1             PGN_IMT1
#define PGN_WATER_IN_FUEL      65279u

/* Legacy SPN names retained for source compatibility. */
#define SPN_ENGINE_TORQUE_MODE         899u
#define SPN_DRIVER_DEMAND_TORQUE       512u
#define SPN_ACTUAL_ENGINE_TORQUE       513u
#define SPN_ENGINE_SPEED               190u
#define SPN_ENGINE_DEMAND_TORQUE       2432u
#define SPN_ACCEL_PEDAL_POS            91u
#define SPN_ENGINE_LOAD_PCT            92u
#define SPN_COOLANT_TEMP               110u
#define SPN_ENGINE_OIL_TEMP            175u
#define SPN_ENGINE_OIL_LEVEL           98u
#define SPN_ENGINE_OIL_PRESSURE        100u
#define SPN_VEHICLE_SPEED              84u
#define SPN_STEERING_ANGLE             1807u
#define SPN_YAW_RATE                   1808u
#define SPN_LATERAL_ACCEL              1809u
#define SPN_LONG_ACCEL                 1810u
#define SPN_TRANS_GEAR                 523u
#define SPN_TRANS_OIL_PRESSURE         127u
#define SPN_TRANS_OIL_TEMP             177u
#define SPN_BRAKE_PEDAL_POS            521u
#define SPN_BRAKE_APP_PRESSURE         116u
#define SPN_RETARDER_TORQUE            520u
#define SPN_SCR_DOSING_RATE            4331u
#define SPN_SCR_TANK_LEVEL             1761u
#define SPN_SCR_TANK_TEMP              3031u
#define SPN_DPF_INLET_TEMP             3250u
#define SPN_DPF_OUTLET_TEMP            3246u
#define SPN_BARO_PRESSURE              108u
#define SPN_CAB_TEMP                   170u
#define SPN_AMBIENT_TEMP               171u
#define SPN_AIR_INLET_TEMP             172u
#define SPN_ROAD_SURFACE_TEMP          79u
#define SPN_ALTERNATOR_CURRENT         115u
#define SPN_BATTERY_VOLTAGE            167u
#define SPN_BATTERY_POTENTIAL          168u
#define SPN_FAN_DRIVE_PCT              1639u
#define SPN_FAN_SPEED                  975u
#define SPN_WASHER_FLUID_LEVEL         80u
#define SPN_FUEL_LEVEL                 96u
#define SPN_SECONDS                    959u
#define SPN_MINUTES                    960u
#define SPN_HOURS                      961u
#define SPN_MONTH                      963u
#define SPN_DAY                        962u
#define SPN_YEAR                       964u
#define SPN_TURBO_BOOST_PRESS          1127u
#define SPN_TURBO_SPEED                103u
#define SPN_WATER_IN_FUEL_FLAG         97u
#define SPN_INTAKE_MANIFOLD_PRESSURE   102u
#define SPN_ENGINE_FUEL_RATE           183u
#define SPN_ENGINE_INST_FUEL_ECON      184u
#define SPN_ENGINE_AVG_FUEL_ECON       185u

typedef enum {
    J1939_TX_MODE_PERIODIC = 0,
    J1939_TX_MODE_ON_CHANGE,
    J1939_TX_MODE_ENGINE_SPEED_DEPENDENT,
    J1939_TX_MODE_ON_REQUEST
} J1939_Transmission_Mode_t;

typedef struct {
    uint32_t                    pgn;
    uint32_t                    spn;
    const char*                 name;
    const char*                 unit;
    uint32_t                    tx_period_ms;  /* Period, maximum/fallback period, or 0 if not fixed. */
    J1939_Transmission_Mode_t   tx_mode;
    uint8_t                     start_byte;    /* 1-based byte position; may exceed 8 for TP payloads. */
    uint8_t                     start_bit;     /* 1-based bit position inside start_byte. */
    uint16_t                    num_bits;      /* Up to 1600 bits in the source metadata. */
    float                       resolution;
    float                       offset;
    float                       min_physical;
    float                       max_physical;
} J1939_Signal_Definition_t;

extern const J1939_Signal_Definition_t J1939_Signal_Database[];
extern const uint16_t J1939_Signal_Count;
extern const uint16_t J1939_Periodic_Signal_Count;
extern const uint16_t J1939_Event_Signal_Count;
extern const uint16_t J1939_On_Request_Signal_Count;

/* SPN-only lookup returns the first match. Use the PGN+SPN lookup for SPNs
 * that legitimately occur in more than one PGN (currently 928 and 1807). */
const J1939_Signal_Definition_t* J1939_Find_Signal_By_SPN(uint32_t spn_number);
/* Returns a definition only when the SPN has exactly one database entry.
 * match_count may be NULL; otherwise it receives 0, 1, or the duplicate count. */
const J1939_Signal_Definition_t* J1939_Find_Unique_Signal_By_SPN(
    uint32_t spn_number,
    uint16_t* match_count);
const J1939_Signal_Definition_t* J1939_Find_Signal_By_PGN_SPN(uint32_t pgn,
                                                              uint32_t spn_number);

#ifdef __cplusplus
}
#endif
#endif /* J1939_SIGNAL_DEFINITIONS_H */
