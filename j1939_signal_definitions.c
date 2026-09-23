/*
 * =============================================================================
 * FILE : j1939_signal_definitions.c
 * WHAT : Complete J1939 signal database table.
 * =============================================================================
 */

#include "j1939_signal_definitions.h"
#include <stddef.h>

/* =============================================================================
 * THE SIGNAL DATABASE
 * Grouped by PGN for readability
 * ============================================================================= */
const J1939_Signal_Definition_t J1939_Signal_Database[] = {

    /* =========================================================================
     * EEC1 PGN 61444 - 10 ms - Engine speed, torques
     * ========================================================================= */
    {
        .pgn = PGN_EEC1,
        .spn = SPN_ENGINE_TORQUE_MODE,
        .name = "Engine Torque Mode",
        .unit = "state",
        .start_byte = 1, .start_bit = 1, .num_bits = 4,
        .resolution = 1.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 15.0f
    },
    {
        .pgn = PGN_EEC1,
        .spn = SPN_DRIVER_DEMAND_TORQUE,
        .name = "Driver Demand Engine Torque",
        .unit = "%",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -125.0f,
        .min_physical = -125.0f, .max_physical = 125.0f
    },
    {
        .pgn = PGN_EEC1,
        .spn = SPN_ACTUAL_ENGINE_TORQUE,
        .name = "Actual Engine Percent Torque",
        .unit = "%",
        .start_byte = 3, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -125.0f,
        .min_physical = -125.0f, .max_physical = 125.0f
    },
    {
        .pgn = PGN_EEC1,
        .spn = SPN_ENGINE_SPEED,
        .name = "Engine Speed",
        .unit = "rpm",
        .start_byte = 4, .start_bit = 1, .num_bits = 16,
        .resolution = 0.125f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 8031.875f
    },
    {
        .pgn = PGN_EEC1,
        .spn = SPN_ENGINE_DEMAND_TORQUE,
        .name = "Engine Demand Percent Torque",
        .unit = "%",
        .start_byte = 8, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -125.0f,
        .min_physical = -125.0f, .max_physical = 125.0f
    },

    /* =========================================================================
     * EEC2 PGN 61442 - 50 ms
     * ========================================================================= */
    {
        .pgn = PGN_EEC2,
        .spn = SPN_ACCEL_PEDAL_POS,
        .name = "Accelerator Pedal Position 1",
        .unit = "%",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 0.4f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 100.0f
    },
    {
        .pgn = PGN_EEC2,
        .spn = SPN_ENGINE_LOAD_PCT,
        .name = "Engine Percent Load At Current Speed",
        .unit = "%",
        .start_byte = 3, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 125.0f
    },

    /* =========================================================================
     * Engine Temperature 1 PGN 65262 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_ENGINE_TEMP1,
        .spn = SPN_COOLANT_TEMP,
        .name = "Engine Coolant Temperature",
        .unit = "degC",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -40.0f,
        .min_physical = -40.0f, .max_physical = 210.0f
    },
    {
        .pgn = PGN_ENGINE_TEMP1,
        .spn = SPN_ENGINE_OIL_TEMP,
        .name = "Engine Oil Temperature 1",
        .unit = "degC",
        .start_byte = 2, .start_bit = 1, .num_bits = 16,
        .resolution = 0.03125f, .offset = -273.0f,
        .min_physical = -273.0f, .max_physical = 1735.0f
    },

    /* =========================================================================
     * Engine Fluids Level/Pressure 1 PGN 65263 - 500 ms
     * ========================================================================= */
    {
        .pgn = PGN_ENGINE_FLUIDS1,
        .spn = SPN_ENGINE_OIL_LEVEL,
        .name = "Engine Oil Level",
        .unit = "%",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 0.4f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 100.0f
    },
    {
        .pgn = PGN_ENGINE_FLUIDS1,
        .spn = SPN_ENGINE_OIL_PRESSURE,
        .name = "Engine Oil Pressure",
        .unit = "kPa",
        .start_byte = 4, .start_bit = 1, .num_bits = 8,
        .resolution = 4.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 1000.0f
    },

    /* =========================================================================
     * Engine Fluids 2 PGN 65270 - 500 ms
     * ========================================================================= */
    {
        .pgn = PGN_ENGINE_FLUIDS2,
        .spn = SPN_INTAKE_MANIFOLD_PRESSURE,
        .name = "Engine Intake Manifold #1 Pressure",
        .unit = "kPa",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 2.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 500.0f
    },

    /* =========================================================================
     * CCVS1 PGN 65265 - 100 ms
     * ========================================================================= */
    {
        .pgn = PGN_CCVS1,
        .spn = SPN_VEHICLE_SPEED,
        .name = "Wheel-Based Vehicle Speed",
        .unit = "km/h",
        .start_byte = 2, .start_bit = 1, .num_bits = 16,
        .resolution = 0.00390625f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 250.996f
    },

    /* =========================================================================
     * VDS PGN 61449 - 50 ms
     * ========================================================================= */
    {
        .pgn = PGN_VDS,
        .spn = SPN_STEERING_ANGLE,
        .name = "Steering Wheel Angle",
        .unit = "rad",
        .start_byte = 1, .start_bit = 1, .num_bits = 16,
        .resolution = 0.0009765625f, .offset = -31.374f,
        .min_physical = -31.374f, .max_physical = 31.374f
    },
    {
        .pgn = PGN_VDS,
        .spn = SPN_YAW_RATE,
        .name = "Yaw Rate",
        .unit = "rad/s",
        .start_byte = 4, .start_bit = 1, .num_bits = 16,
        .resolution = 0.00012207031f, .offset = -3.92f,
        .min_physical = -3.92f, .max_physical = 3.92f
    },
    {
        .pgn = PGN_VDS,
        .spn = SPN_LATERAL_ACCEL,
        .name = "Lateral Acceleration",
        .unit = "m/s2",
        .start_byte = 6, .start_bit = 1, .num_bits = 16,
        .resolution = 0.00048828125f, .offset = -15.687f,
        .min_physical = -15.687f, .max_physical = 15.687f
    },
    {
        .pgn = PGN_VDS,
        .spn = SPN_LONG_ACCEL,
        .name = "Longitudinal Acceleration",
        .unit = "m/s2",
        .start_byte = 8, .start_bit = 1, .num_bits = 8,
        .resolution = 0.1f, .offset = -12.5f,
        .min_physical = -12.5f, .max_physical = 12.5f
    },

    /* =========================================================================
     * ETC2 PGN 61445 - 50 ms
     * ========================================================================= */
    {
        .pgn = PGN_ETC2,
        .spn = SPN_TRANS_GEAR,
        .name = "Transmission Current Gear",
        .unit = "gear",
        .start_byte = 4, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -125.0f,
        .min_physical = -125.0f, .max_physical = 125.0f
    },

    /* =========================================================================
     * Transmission Fluids 1 PGN 65272 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_TRANS_FLUIDS1,
        .spn = SPN_TRANS_OIL_PRESSURE,
        .name = "Transmission Oil Pressure",
        .unit = "kPa",
        .start_byte = 4, .start_bit = 1, .num_bits = 8,
        .resolution = 16.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 4000.0f
    },
    {
        .pgn = PGN_TRANS_FLUIDS1,
        .spn = SPN_TRANS_OIL_TEMP,
        .name = "Transmission Oil Temperature",
        .unit = "degC",
        .start_byte = 5, .start_bit = 1, .num_bits = 16,
        .resolution = 0.03125f, .offset = -273.0f,
        .min_physical = -273.0f, .max_physical = 1735.0f
    },

    /* =========================================================================
     * EBC1 PGN 61441 - 100 ms
     * ========================================================================= */
    {
        .pgn = PGN_EBC1,
        .spn = SPN_BRAKE_PEDAL_POS,
        .name = "Brake Pedal Position",
        .unit = "%",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 0.4f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 100.0f
    },

    /* =========================================================================
     * Brake Air Pressure PGN 65274 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_BRAKE_AIR_PRESS,
        .spn = SPN_BRAKE_APP_PRESSURE,
        .name = "Brake Application Pressure",
        .unit = "kPa",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 4.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 1000.0f
    },

    /* =========================================================================
     * ERC1 PGN 61440 - 50 ms
     * ========================================================================= */
    {
        .pgn = PGN_ERC1,
        .spn = SPN_RETARDER_TORQUE,
        .name = "Actual Retarder Percent Torque",
        .unit = "%",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -125.0f,
        .min_physical = -125.0f, .max_physical = 0.0f
    },

    /* =========================================================================
     * AT1 SCR Dosing PGN 61475 - 100 ms
     * ========================================================================= */
    {
        .pgn = PGN_AT1_SCR_DOSING,
        .spn = SPN_SCR_DOSING_RATE,
        .name = "SCR Actual Dosing Reagent Quantity",
        .unit = "g/h",
        .start_byte = 1, .start_bit = 1, .num_bits = 16,
        .resolution = 0.3f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 19276.5f
    },

    /* =========================================================================
     * AT1 SCR Tank PGN 65110 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_AT1_SCR_TANK,
        .spn = SPN_SCR_TANK_LEVEL,
        .name = "SCR Catalyst Tank Level",
        .unit = "%",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 0.4f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 100.0f
    },
    {
        .pgn = PGN_AT1_SCR_TANK,
        .spn = SPN_SCR_TANK_TEMP,
        .name = "SCR Catalyst Tank Temperature",
        .unit = "degC",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -40.0f,
        .min_physical = -40.0f, .max_physical = 210.0f
    },

    /* =========================================================================
     * DPF Temperature 1 PGN 64947 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_DPF_TEMP1,
        .spn = SPN_DPF_INLET_TEMP,
        .name = "DPF Inlet Gas Temperature",
        .unit = "degC",
        .start_byte = 1, .start_bit = 1, .num_bits = 16,
        .resolution = 0.03125f, .offset = -273.0f,
        .min_physical = -273.0f, .max_physical = 1735.0f
    },
    {
        .pgn = PGN_DPF_TEMP1,
        .spn = SPN_DPF_OUTLET_TEMP,
        .name = "DPF Outlet Gas Temperature",
        .unit = "degC",
        .start_byte = 3, .start_bit = 1, .num_bits = 16,
        .resolution = 0.03125f, .offset = -273.0f,
        .min_physical = -273.0f, .max_physical = 1735.0f
    },

    /* =========================================================================
     * AMB PGN 65269 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_AMB,
        .spn = SPN_BARO_PRESSURE,
        .name = "Barometric Pressure",
        .unit = "kPa",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 0.5f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 125.0f
    },
    {
        .pgn = PGN_AMB,
        .spn = SPN_CAB_TEMP,
        .name = "Cab Interior Temperature",
        .unit = "degC",
        .start_byte = 2, .start_bit = 1, .num_bits = 16,
        .resolution = 0.03125f, .offset = -273.0f,
        .min_physical = -273.0f, .max_physical = 1735.0f
    },
    {
        .pgn = PGN_AMB,
        .spn = SPN_AMBIENT_TEMP,
        .name = "Ambient Air Temperature",
        .unit = "degC",
        .start_byte = 4, .start_bit = 1, .num_bits = 16,
        .resolution = 0.03125f, .offset = -273.0f,
        .min_physical = -273.0f, .max_physical = 1735.0f
    },
    {
        .pgn = PGN_AMB,
        .spn = SPN_AIR_INLET_TEMP,
        .name = "Engine Air Inlet Temperature",
        .unit = "degC",
        .start_byte = 6, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -40.0f,
        .min_physical = -40.0f, .max_physical = 210.0f
    },
    {
        .pgn = PGN_AMB,
        .spn = SPN_ROAD_SURFACE_TEMP,
        .name = "Road Surface Temperature",
        .unit = "degC",
        .start_byte = 7, .start_bit = 1, .num_bits = 16,
        .resolution = 0.03125f, .offset = -273.0f,
        .min_physical = -273.0f, .max_physical = 1735.0f
    },

    /* =========================================================================
     * Electrical PGN 65271 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_VOLTS_AMPS,
        .spn = SPN_ALTERNATOR_CURRENT,
        .name = "Alternator Current",
        .unit = "A",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = -125.0f,
        .min_physical = -125.0f, .max_physical = 125.0f
    },
    {
        .pgn = PGN_VOLTS_AMPS,
        .spn = SPN_BATTERY_VOLTAGE,
        .name = "Battery Potential (Voltage)",
        .unit = "V",
        .start_byte = 3, .start_bit = 1, .num_bits = 16,
        .resolution = 0.05f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 3212.75f
    },
    {
        .pgn = PGN_VOLTS_AMPS,
        .spn = SPN_BATTERY_POTENTIAL,
        .name = "Battery Potential / Power Input 1",
        .unit = "V",
        .start_byte = 5, .start_bit = 1, .num_bits = 16,
        .resolution = 0.05f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 3212.75f
    },

    /* =========================================================================
     * Fan Drive PGN 65213 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_FAN_DRIVE,
        .spn = SPN_FAN_DRIVE_PCT,
        .name = "Estimated Fan Speed Percent",
        .unit = "%",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 0.4f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 100.0f
    },
    {
        .pgn = PGN_FAN_DRIVE,
        .spn = SPN_FAN_SPEED,
        .name = "Fan Speed",
        .unit = "rpm",
        .start_byte = 3, .start_bit = 1, .num_bits = 16,
        .resolution = 0.125f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 8031.875f
    },

    /* =========================================================================
     * Dash / Fuel PGN 65276 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_DASH,
        .spn = SPN_WASHER_FLUID_LEVEL,
        .name = "Washer Fluid Level",
        .unit = "%",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 0.4f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 100.0f
    },
    {
        .pgn = PGN_DASH,
        .spn = SPN_FUEL_LEVEL,
        .name = "Fuel Level 1",
        .unit = "%",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 0.4f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 100.0f
    },

    /* =========================================================================
     * Time / Date PGN 65254 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_TIME_DATE,
        .spn = SPN_SECONDS,
        .name = "Seconds",
        .unit = "s",
        .start_byte = 1, .start_bit = 1, .num_bits = 8,
        .resolution = 0.25f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 59.75f
    },
    {
        .pgn = PGN_TIME_DATE,
        .spn = SPN_MINUTES,
        .name = "Minutes",
        .unit = "min",
        .start_byte = 2, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 59.0f
    },
    {
        .pgn = PGN_TIME_DATE,
        .spn = SPN_HOURS,
        .name = "Hours",
        .unit = "hr",
        .start_byte = 3, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 23.0f
    },
    {
        .pgn = PGN_TIME_DATE,
        .spn = SPN_MONTH,
        .name = "Month",
        .unit = "month",
        .start_byte = 4, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = 0.0f,
        .min_physical = 1.0f, .max_physical = 12.0f
    },
    {
        .pgn = PGN_TIME_DATE,
        .spn = SPN_DAY,
        .name = "Day",
        .unit = "day",
        .start_byte = 5, .start_bit = 1, .num_bits = 8,
        .resolution = 0.25f, .offset = 0.0f,
        .min_physical = 0.25f, .max_physical = 31.75f
    },
    {
        .pgn = PGN_TIME_DATE,
        .spn = SPN_YEAR,
        .name = "Year",
        .unit = "yr",
        .start_byte = 6, .start_bit = 1, .num_bits = 8,
        .resolution = 1.0f, .offset = 1985.0f,
        .min_physical = 1985.0f, .max_physical = 2235.0f
    },

    /* =========================================================================
     * Turbocharger 1 PGN 65190 - 500 ms
     * ========================================================================= */
    {
        .pgn = PGN_TURBO1,
        .spn = SPN_TURBO_BOOST_PRESS,
        .name = "Turbocharger 1 Boost Pressure",
        .unit = "kPa",
        .start_byte = 1, .start_bit = 1, .num_bits = 16,
        .resolution = 0.125f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 8031.875f
    },
    {
        .pgn = PGN_TURBO1,
        .spn = SPN_TURBO_SPEED,
        .name = "Turbocharger Speed",
        .unit = "rpm",
        .start_byte = 3, .start_bit = 1, .num_bits = 16,
        .resolution = 1.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 65535.0f
    },

    /* =========================================================================
     * Water In Fuel PGN 65279 - 1000 ms
     * ========================================================================= */
    {
        .pgn = PGN_WATER_IN_FUEL,
        .spn = SPN_WATER_IN_FUEL_FLAG,
        .name = "Water In Fuel Indicator",
        .unit = "state",
        .start_byte = 1, .start_bit = 1, .num_bits = 2,
        .resolution = 1.0f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 3.0f
    },

    /* =========================================================================
     * Fuel Economy Liquid PGN 65266 - 100 ms
     * ========================================================================= */
    {
        .pgn = PGN_LFE,
        .spn = SPN_ENGINE_FUEL_RATE,
        .name = "Engine Fuel Rate",
        .unit = "L/h",
        .start_byte = 1, .start_bit = 1, .num_bits = 16,
        .resolution = 0.05f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 3212.75f
    },
    {
        .pgn = PGN_LFE,
        .spn = SPN_ENGINE_INST_FUEL_ECON,
        .name = "Engine Instantaneous Fuel Economy",
        .unit = "km/L",
        .start_byte = 3, .start_bit = 1, .num_bits = 16,
        .resolution = 0.001953125f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 125.5f
    },
    {
        .pgn = PGN_LFE,
        .spn = SPN_ENGINE_AVG_FUEL_ECON,
        .name = "Engine Average Fuel Economy",
        .unit = "km/L",
        .start_byte = 5, .start_bit = 1, .num_bits = 16,
        .resolution = 0.001953125f, .offset = 0.0f,
        .min_physical = 0.0f, .max_physical = 125.5f
    },
};

const uint16_t J1939_Signal_Count = 
    (uint16_t)(sizeof(J1939_Signal_Database) / sizeof(J1939_Signal_Definition_t));


/* =============================================================================
 * J1939_Find_Signal_By_SPN
 * ============================================================================= */
const J1939_Signal_Definition_t* J1939_Find_Signal_By_SPN(uint32_t spn_number)
{
    for (uint16_t i = 0u; i < J1939_Signal_Count; i++) {
        if (J1939_Signal_Database[i].spn == spn_number) {
            return &J1939_Signal_Database[i];
        }
    }
    return NULL;
}