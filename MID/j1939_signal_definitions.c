/*
 * =============================================================================
 * FILE : j1939_signal_definitions.c
 * WHAT : Complete 179-entry J1939 signal database.
 * SOURCE: doc/J1939_179_SPN_Master_Specification_Table.xlsx,
 *         sheet "Master 179 SPN Database", rows 6-184.
 *
 * ORDER: periodic PGNs by ascending normalized period, event-driven PGNs,
 *        then on-request PGNs. Within each PGN, SPNs are ordered by byte/bit.
 * =============================================================================
 */

#include "j1939_signal_definitions.h"
#include <stddef.h>

#define J1939_SIGNAL(PGN, SPN, NAME, UNIT, PERIOD_MS, MODE, BYTE, BIT, BITS, RES, OFF, MIN, MAX) \
    { (PGN), (SPN), (NAME), (UNIT), (PERIOD_MS), (MODE), (BYTE), (BIT), (BITS), \
      (RES), (OFF), (MIN), (MAX) }

const J1939_Signal_Definition_t J1939_Signal_Database[] = {

    /* =====================================================================
     * PERIODIC: 10 ms
     * ===================================================================== */

    /* PGN_EBS11: PGN 712 (0x02C8), Tx Rate: 10 ms */
    J1939_SIGNAL(PGN_EBS11, 5113u, "Vehicle ABS Active", "State",
                 10u, J1939_TX_MODE_PERIODIC, 1u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EBS11, 5114u, "Brake Light Switch", "State",
                 10u, J1939_TX_MODE_PERIODIC, 2u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_ETC1: PGN 61442 (0xF002), Tx Rate: 10 ms */
    J1939_SIGNAL(PGN_ETC1, 574u, "Transmission Shift In Process", "State",
                 10u, J1939_TX_MODE_PERIODIC, 1u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_ETC1, 522u, "Percent Clutch Slip", "%",
                 10u, J1939_TX_MODE_PERIODIC, 4u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),

    /* PGN_VDC2: PGN 61449 (0xF009), Tx Rate: 10 ms */
    J1939_SIGNAL(PGN_VDC2, 1807u, "Steering Wheel Angle", "deg",
                 10u, J1939_TX_MODE_PERIODIC, 1u, 1u, 16u,
                 0.0009765625f, -31.374f, -31.374f, 31.374f),
    J1939_SIGNAL(PGN_VDC2, 1808u, "Yaw Rate", "rad/s",
                 10u, J1939_TX_MODE_PERIODIC, 4u, 1u, 16u,
                 0.0001220703125f, -3.92f, -3.92f, 3.92f),
    J1939_SIGNAL(PGN_VDC2, 1809u, "Lateral Acceleration", "m/s2",
                 10u, J1939_TX_MODE_PERIODIC, 6u, 1u, 16u,
                 0.00048828125f, -15.687f, -15.687f, 15.687f),
    J1939_SIGNAL(PGN_VDC2, 1810u, "Longitudinal Acceleration", "m/s2",
                 10u, J1939_TX_MODE_PERIODIC, 8u, 1u, 8u,
                 0.1f, -12.5f, -12.5f, 12.5f),

    /* PGN_SWA: PGN 61469 (0xF01D), Tx Rate: 10 ms */
    J1939_SIGNAL(PGN_SWA, 1807u, "Steering Wheel Angle", "deg",
                 10u, J1939_TX_MODE_PERIODIC, 1u, 1u, 16u,
                 0.0009765625f, -31.374f, -31.374f, 31.374f),

    /* =====================================================================
     * PERIODIC: 20 ms
     * ===================================================================== */

    /* PGN_EAC2: PGN 61451 (0xF00B), Tx Rate: 20 ms */
    J1939_SIGNAL(PGN_EAC2, 928u, "Axle Location", "State",
                 20u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 255.0f),

    /* PGN_HRW: PGN 65134 (0xFE6E), Tx Rate: 20 ms */
    J1939_SIGNAL(PGN_HRW, 1592u, "Front Axle, Left Wheel Speed", "km/h",
                 20u, J1939_TX_MODE_PERIODIC, 1u, 1u, 16u,
                 0.00390625f, 0.0f, 0.0f, 250.996f),
    J1939_SIGNAL(PGN_HRW, 1593u, "Front axle, right wheel speed", "km/h",
                 20u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.00390625f, 0.0f, 0.0f, 250.996f),
    J1939_SIGNAL(PGN_HRW, 1594u, "Rear axle, left wheel speed", "km/h",
                 20u, J1939_TX_MODE_PERIODIC, 5u, 1u, 16u,
                 0.00390625f, 0.0f, 0.0f, 250.996f),
    J1939_SIGNAL(PGN_HRW, 1595u, "Rear axle, right wheel speed", "km/h",
                 20u, J1939_TX_MODE_PERIODIC, 7u, 1u, 16u,
                 0.00390625f, 0.0f, 0.0f, 250.996f),

    /* =====================================================================
     * PERIODIC: 50 ms
     * ===================================================================== */

    /* PGN_EEC2: PGN 61443 (0xF003), Tx Rate: 50 ms */
    J1939_SIGNAL(PGN_EEC2, 558u, "Accelerator Pedal 1 Low Idle Switch", "State",
                 50u, J1939_TX_MODE_PERIODIC, 1u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EEC2, 559u, "Accelerator Pedal Kickdown Switch", "State",
                 50u, J1939_TX_MODE_PERIODIC, 1u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EEC2, 1437u, "Road Speed Limit Status", "State",
                 50u, J1939_TX_MODE_PERIODIC, 1u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EEC2, 91u, "Accelerator Pedal Position 1", "%",
                 50u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),
    J1939_SIGNAL(PGN_EEC2, 92u, "Engine Percent Load At Current Speed", "%",
                 50u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_EEC2, 29u, "Accelerator Pedal Position 2", "%",
                 50u, J1939_TX_MODE_PERIODIC, 5u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),

    /* PGN_TCO1: PGN 65132 (0xFE6C), Tx Rate: 50 ms */
    J1939_SIGNAL(PGN_TCO1, 1617u, "Driver 1 Time Related States", "State",
                 50u, J1939_TX_MODE_PERIODIC, 2u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_TCO1, 1614u, "Vehicle Overspeed", "State",
                 50u, J1939_TX_MODE_PERIODIC, 2u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_TCO1, 1618u, "Driver 2 Time Related States", "State",
                 50u, J1939_TX_MODE_PERIODIC, 3u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_TCO1, 1622u, "System event", "State",
                 50u, J1939_TX_MODE_PERIODIC, 4u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_TCO1, 1621u, "Handling information", "State",
                 50u, J1939_TX_MODE_PERIODIC, 4u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_TCO1, 1620u, "Tachograph performance", "State",
                 50u, J1939_TX_MODE_PERIODIC, 4u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_TCO1, 1619u, "Direction indicator", "State",
                 50u, J1939_TX_MODE_PERIODIC, 4u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_TCO1, 1623u, "Tachograph output shaft speed", "rpm",
                 50u, J1939_TX_MODE_PERIODIC, 5u, 1u, 16u,
                 0.125f, 0.0f, 0.0f, 8031.875f),
    J1939_SIGNAL(PGN_TCO1, 1624u, "Tachograph vehicle speed", "km/h",
                 50u, J1939_TX_MODE_PERIODIC, 7u, 1u, 16u,
                 0.00390625f, 0.0f, 0.0f, 250.996f),

    /* =====================================================================
     * PERIODIC: 100 ms
     * ===================================================================== */

    /* PGN_ERC1: PGN 61440 (0xF000), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_ERC1, 900u, "Retarder Torque Mode", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_ERC1, 520u, "Actual Retarder - Percent Torque", "%",
                 100u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_ERC1, 1715u, "Drivers Demand Retarder - Percent Torque", "%",
                 100u, J1939_TX_MODE_PERIODIC, 6u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_ERC1, 1717u, "Actual Maximum Available Retarder - Percent Torque", "%",
                 100u, J1939_TX_MODE_PERIODIC, 8u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),

    /* PGN_EBC1: PGN 61441 (0xF001), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_EBC1, 563u, "Anti-Lock Braking (ABS) Active", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EBC1, 1121u, "EBS Brake Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EBC1, 521u, "Brake Pedal Position", "%",
                 100u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),
    J1939_SIGNAL(PGN_EBC1, 1481u, "Source Address of Controlling Device for Brake Control", "SA",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 255.0f),
    J1939_SIGNAL(PGN_EBC1, 2911u, "Halt brake switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 8u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_ETC2: PGN 61445 (0xF005), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_ETC2, 524u, "Transmission Selected Gear", "gear",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_ETC2, 526u, "Transmission Actual Gear Ratio", "Ratio",
                 100u, J1939_TX_MODE_PERIODIC, 2u, 1u, 16u,
                 0.001f, 0.0f, 0.0f, 64.255f),
    J1939_SIGNAL(PGN_ETC2, 523u, "Transmission Current Gear", "gear",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),

    /* PGN_PTODE: PGN 64932 (0xFDA4), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_PTODE, 3948u, "At least one PTO engaged", "State",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_DC2: PGN 64933 (0xFDA5), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_DC2, 3412u, "Lock Status of Door 1", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_DC2, 3413u, "Open Status of Door 1", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_DC2, 3414u, "Enable Status of Door 1", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_DC2, 3415u, "Lock Status of Door 2", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_DC2, 3416u, "Open Status of Door 2", "State",
                 100u, J1939_TX_MODE_PERIODIC, 2u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_DC2, 3417u, "Enable Status of Door 2", "State",
                 100u, J1939_TX_MODE_PERIODIC, 2u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_FLI2: PGN 65115 (0xFE5B), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_FLI2, 1711u, "Lane Tracking Status Right Side", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_FLI2, 1710u, "Lane Tracking Status Left Side", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_ACC1: PGN 65135 (0xFE6F), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_ACC1, 1586u, "Speed of forward vehicle", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_ACC1, 1587u, "Distance to forward vehicle", "m",
                 100u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_ACC1, 1588u, "Adaptive Cruise Control Set Speed", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_ACC1, 1590u, "Adaptive Cruise Control Mode", "State",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 1u, 3u,
                 1.0f, 0.0f, 0.0f, 7.0f),
    J1939_SIGNAL(PGN_ACC1, 1589u, "Adaptive cruise control set distance mode", "State",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 4u, 3u,
                 1.0f, 0.0f, 0.0f, 7.0f),
    J1939_SIGNAL(PGN_ACC1, 1798u, "ACC Target Detected", "State",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_ACC1, 1797u, "ACC System Shutoff Warning", "State",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_EI: PGN 65170 (0xFE92), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_EI, 1241u, "Engine Gas Mass Flow Rate 1", "kg/h",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 1u, 16u,
                 0.05f, 0.0f, 0.0f, 3212.75f),

    /* PGN_EBC2: PGN 65215 (0xFEBF), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_EBC2, 904u, "Front Axle Speed", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 1u, 16u,
                 0.00390625f, 0.0f, 0.0f, 250.996f),
    J1939_SIGNAL(PGN_EBC2, 905u, "Relative Speed; Front Axle, Left Wheel", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 0.0625f, -7.8125f, -7.8125f, 7.8125f),
    J1939_SIGNAL(PGN_EBC2, 906u, "Relative Speed; Front Axle, Right Wheel", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 1u, 8u,
                 0.0625f, -7.8125f, -7.8125f, 7.8125f),
    J1939_SIGNAL(PGN_EBC2, 907u, "Relative Speed; Rear Axle #1, Left Wheel", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 1u, 8u,
                 0.0625f, -7.8125f, -7.8125f, 7.8125f),
    J1939_SIGNAL(PGN_EBC2, 908u, "Relative Speed; Rear Axle #1, Right Wheel", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 6u, 1u, 8u,
                 0.0625f, -7.8125f, -7.8125f, 7.8125f),
    J1939_SIGNAL(PGN_EBC2, 909u, "Relative Speed; Rear Axle #2, Left Wheel", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 1u, 8u,
                 0.0625f, -7.8125f, -7.8125f, 7.8125f),
    J1939_SIGNAL(PGN_EBC2, 910u, "Relative Speed; Rear Axle #2, Right Wheel", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 8u, 1u, 8u,
                 0.0625f, -7.8125f, -7.8125f, 7.8125f),

    /* PGN_VD: PGN 65248 (0xFEE0), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_VD, 244u, "Trip Distance", "m",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 1u, 32u,
                 0.125f, 0.0f, 0.0f, 526385151.9f),
    J1939_SIGNAL(PGN_VD, 245u, "Total Vehicle Distance", "m",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 1u, 32u,
                 0.125f, 0.0f, 0.0f, 526385151.9f),

    /* PGN_CCVS1: PGN 65265 (0xFEF1), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_CCVS1, 70u, "Parking Brake Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 3807u, "Park Brake Release Inhibit Request", "State",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 84u, "Wheel-Based Vehicle Speed", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 2u, 1u, 16u,
                 0.00390625f, 0.0f, 0.0f, 250.996f),
    J1939_SIGNAL(PGN_CCVS1, 595u, "Cruise Control Active", "State",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 596u, "Cruise Control Enable Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 597u, "Brake Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 598u, "Clutch Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 4u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 599u, "Cruise Control Set Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 600u, "Cruise Control Coast (Decelerate) Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 601u, "Cruise Control Resume Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 602u, "Cruise Control Accelerate Switch", "State",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_CCVS1, 86u, "Cruise Control Set Speed", "km/h",
                 100u, J1939_TX_MODE_PERIODIC, 6u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_CCVS1, 976u, "PTO Governor State", "State",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 1u, 5u,
                 1.0f, 0.0f, 0.0f, 31.0f),
    J1939_SIGNAL(PGN_CCVS1, 527u, "Cruise Control States", "State",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 6u, 3u,
                 1.0f, 0.0f, 0.0f, 7.0f),

    /* PGN_LFE: PGN 65266 (0xFEF2), Tx Rate: 100 ms */
    J1939_SIGNAL(PGN_LFE, 183u, "Engine Fuel Rate", "l/h",
                 100u, J1939_TX_MODE_PERIODIC, 1u, 1u, 16u,
                 0.05f, 0.0f, 0.0f, 3212.75f),
    J1939_SIGNAL(PGN_LFE, 184u, "Engine Instantaneous Fuel Economy", "km/L",
                 100u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.001953125f, 0.0f, 0.0f, 125.5f),
    J1939_SIGNAL(PGN_LFE, 185u, "Engine Average Fuel Economy", "km/L",
                 100u, J1939_TX_MODE_PERIODIC, 5u, 1u, 16u,
                 0.001953125f, 0.0f, 0.0f, 125.5f),
    J1939_SIGNAL(PGN_LFE, 51u, "Engine Throttle Position", "%",
                 100u, J1939_TX_MODE_PERIODIC, 7u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),

    /* =====================================================================
     * PERIODIC: 250 ms
     * ===================================================================== */

    /* PGN_SBS: PGN 64791 (0xFD17), Tx Rate: 250 ms */
    J1939_SIGNAL(PGN_SBS, 4952u, "Driver Beltlock Status", "State",
                 250u, J1939_TX_MODE_PERIODIC, 1u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_SBS, 4953u, "Passenger Beltlock Status", "State",
                 250u, J1939_TX_MODE_PERIODIC, 1u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_SBS, 4962u, "Passenger Airbag Deactivation Switch Status", "State",
                 250u, J1939_TX_MODE_PERIODIC, 3u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_EEC3: PGN 65247 (0xFEDF), Tx Rate: 250 msecs */
    J1939_SIGNAL(PGN_EEC3, 514u, "Nominal Friction - Percent Torque", "%",
                 250u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_EEC3, 2978u, "Estimated Engine Parasitic Losses - Percent Torque", "%",
                 250u, J1939_TX_MODE_PERIODIC, 5u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_EEC3, 3236u, "Aftertreatment 1 Exhaust Gas Mass Flow", "kg/h",
                 250u, J1939_TX_MODE_PERIODIC, 6u, 1u, 16u,
                 0.2f, 0.0f, 0.0f, 12851.0f),

    /* =====================================================================
     * PERIODIC: 330 ms
     * ===================================================================== */

    /* PGN_PROPB_FB: PGN 65457 (0xFFB1), Tx Rate: 330 ms */
    J1939_SIGNAL(PGN_PROPB_FB, 5179u, "Turn Signal Switch", "State",
                 330u, J1939_TX_MODE_PERIODIC, 2u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* =====================================================================
     * PERIODIC: 500 ms
     * ===================================================================== */

    /* PGN_EGF2: PGN 64930 (0xFDA2), Tx Rate: 500 ms */
    J1939_SIGNAL(PGN_EGF2, 3467u, "Engine Gas 2 Mass Flow Rate", "kg/h",
                 500u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.05f, 0.0f, 0.0f, 3212.75f),

    /* PGN_IMT1: PGN 65190 (0xFEA6), Tx Rate: 0.5 s */
    J1939_SIGNAL(PGN_IMT1, 1127u, "Engine Turbocharger 1 Boost Pressure", "kPa",
                 500u, J1939_TX_MODE_PERIODIC, 1u, 1u, 16u,
                 0.125f, 0.0f, 0.0f, 8031.875f),
    J1939_SIGNAL(PGN_IMT1, 1128u, "Engine Turbocharger 2 Boost Pressure", "kPa",
                 500u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.125f, 0.0f, 0.0f, 8031.875f),
    J1939_SIGNAL(PGN_IMT1, 1129u, "Engine Turbocharger 3 Boost Pressure", "kPa",
                 500u, J1939_TX_MODE_PERIODIC, 5u, 1u, 16u,
                 0.125f, 0.0f, 0.0f, 8031.875f),
    J1939_SIGNAL(PGN_IMT1, 1130u, "Engine Turbocharger 4 Boost Pressure", "kPa",
                 500u, J1939_TX_MODE_PERIODIC, 7u, 1u, 16u,
                 0.125f, 0.0f, 0.0f, 8031.875f),

    /* PGN_EFL_P1: PGN 65263 (0xFEEF), Tx Rate: 0.5 s */
    J1939_SIGNAL(PGN_EFL_P1, 98u, "Engine Oil Level", "%",
                 500u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),
    J1939_SIGNAL(PGN_EFL_P1, 100u, "Engine Oil Pressure", "kPa",
                 500u, J1939_TX_MODE_PERIODIC, 4u, 1u, 8u,
                 4.0f, 0.0f, 0.0f, 1000.0f),

    /* PGN_IC1: PGN 65270 (0xFEF6), Tx Rate: 0.5 s */
    J1939_SIGNAL(PGN_IC1, 102u, "Engine Intake Manifold #1 Pressure", "kPa",
                 500u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 2.0f, 0.0f, 0.0f, 500.0f),
    J1939_SIGNAL(PGN_IC1, 105u, "Engine Intake Manifold 1 Temperature", "degC",
                 500u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),
    J1939_SIGNAL(PGN_IC1, 106u, "Engine Air Inlet Pressure", "kPa",
                 500u, J1939_TX_MODE_PERIODIC, 4u, 1u, 8u,
                 2.0f, 0.0f, 0.0f, 500.0f),
    J1939_SIGNAL(PGN_IC1, 173u, "Engine Exhaust Gas Temperature", "degC",
                 500u, J1939_TX_MODE_PERIODIC, 6u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),

    /* =====================================================================
     * PERIODIC: 1000 ms
     * ===================================================================== */

    /* PGN_CM1: PGN 57344 (0xE000), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_CM1, 1856u, "Seat Belt Switch", "State",
                 1000u, J1939_TX_MODE_PERIODIC, 4u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_HRFC: PGN 64777 (0xFD09), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_HRFC, 5053u, "Engine Trip Fuel (High Resolution)", "l",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 32u,
                 0.001f, 0.0f, 0.0f, 4211081.215f),
    J1939_SIGNAL(PGN_HRFC, 5054u, "Engine Total Fuel Used (High Resolution)", "l",
                 1000u, J1939_TX_MODE_PERIODIC, 5u, 1u, 32u,
                 0.001f, 0.0f, 0.0f, 4211081.215f),

    /* PGN_AUXC: PGN 64850 (0xFD52), Tx Rate: 1 sec */
    J1939_SIGNAL(PGN_AUXC, 4196u, "Desired Engine Coolant Pump Outlet Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),

    /* PGN_ET4: PGN 64870 (0xFD66), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_ET4, 4076u, "Engine Coolant Temperature 2", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),

    /* PGN_TI1: PGN 65110 (0xFE56), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_TI1, 1761u, "Aftertreatment 1 Diesel Exhaust Fluid Tank Volume", "%",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),
    J1939_SIGNAL(PGN_TI1, 3031u, "Aftertreatment 1 Diesel Exhaust Fluid Tank Temperature 1", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),
    J1939_SIGNAL(PGN_TI1, 3532u, "Aftertreatment 1 Diesel Exhaust Fluid Tank Level", "m",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.1f, 0.0f, 0.0f, 6425.5f),

    /* PGN_TIRE2: PGN 65118 (0xFE5E), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_TIRE2, 242u, "Tire Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),

    /* PGN_ET3: PGN 65129 (0xFE69), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_ET3, 1637u, "Engine Coolant Temperature (High Resolution)", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),

    /* PGN_ET2: PGN 65188 (0xFEA4), Tx Rate: 1s */
    J1939_SIGNAL(PGN_ET2, 1135u, "Engine Oil Temperature 2", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),

    /* PGN_AIR1: PGN 65198 (0xFEAE), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_AIR1, 1087u, "Service Brake Circuit 1 Air Pressure", "kPa",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 8.0f, 0.0f, 0.0f, 2000.0f),
    J1939_SIGNAL(PGN_AIR1, 1088u, "Service Brake Circuit 2 Air Pressure", "kPa",
                 1000u, J1939_TX_MODE_PERIODIC, 4u, 1u, 8u,
                 8.0f, 0.0f, 0.0f, 2000.0f),

    /* PGN_FD: PGN 65213 (0xFEBD), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_FD, 977u, "Fan Drive State", "State",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_FD, 1639u, "Fan Speed", "rpm",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.125f, 0.0f, 0.0f, 8031.875f),

    /* PGN_VDHR: PGN 65217 (0xFEC1), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_VDHR, 917u, "High Resolution Total Vehicle Distance", "m",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 32u,
                 5.0f, 0.0f, 0.0f, 21055406.0f),
    J1939_SIGNAL(PGN_VDHR, 918u, "High Resolution Trip Distance", "m",
                 1000u, J1939_TX_MODE_PERIODIC, 5u, 1u, 32u,
                 5.0f, 0.0f, 0.0f, 21055406.0f),

    /* PGN_SHUTDOWN: PGN 65252 (0xFEE4), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_SHUTDOWN, 985u, "A/C High Pressure Fan Switch", "State",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_ET1: PGN 65262 (0xFEEE), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_ET1, 110u, "Engine Coolant Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),
    J1939_SIGNAL(PGN_ET1, 174u, "Engine Fuel Temperature 1", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),
    J1939_SIGNAL(PGN_ET1, 175u, "Engine Oil Temperature 1", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),
    J1939_SIGNAL(PGN_ET1, 52u, "Engine Intercooler Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 7u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),

    /* PGN_AMB: PGN 65269 (0xFEF5), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_AMB, 108u, "Barometric Pressure", "kPa",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 0.5f, 0.0f, 0.0f, 125.0f),
    J1939_SIGNAL(PGN_AMB, 170u, "Cab Interior Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),
    J1939_SIGNAL(PGN_AMB, 171u, "Ambient Air Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 4u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),
    J1939_SIGNAL(PGN_AMB, 172u, "Engine Air Inlet Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 6u, 1u, 8u,
                 1.0f, -40.0f, -40.0f, 210.0f),

    /* PGN_VEP: PGN 65271 (0xFEF7), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_VEP, 114u, "Net Battery Current", "Amps",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_VEP, 115u, "Alternator Current", "Amps",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_VEP, 167u, "Charging System Potential (Voltage)", "Volts",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 16u,
                 0.05f, 0.0f, 0.0f, 3212.75f),
    J1939_SIGNAL(PGN_VEP, 168u, "Battery Potential / Power Input 1", "Volts",
                 1000u, J1939_TX_MODE_PERIODIC, 5u, 1u, 16u,
                 0.05f, 0.0f, 0.0f, 3212.75f),
    J1939_SIGNAL(PGN_VEP, 158u, "Keyswitch Battery Potential", "Volts",
                 1000u, J1939_TX_MODE_PERIODIC, 7u, 1u, 16u,
                 0.05f, 0.0f, 0.0f, 3212.75f),

    /* PGN_AI: PGN 65273 (0xFEF9), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_AI, 930u, "Drive Axle Location", "State",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 255.0f),

    /* PGN_B: PGN 65274 (0xFEFA), Tx Rate: 1 s */
    J1939_SIGNAL(PGN_B, 116u, "Brake Application Pressure", "kPa",
                 1000u, J1939_TX_MODE_PERIODIC, 1u, 1u, 8u,
                 4.0f, 0.0f, 0.0f, 1000.0f),
    J1939_SIGNAL(PGN_B, 117u, "Brake Primary Pressure", "kPa",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 4.0f, 0.0f, 0.0f, 1000.0f),
    J1939_SIGNAL(PGN_B, 118u, "Brake Secondary Pressure", "kPa",
                 1000u, J1939_TX_MODE_PERIODIC, 3u, 1u, 8u,
                 4.0f, 0.0f, 0.0f, 1000.0f),

    /* PGN_DD: PGN 65276 (0xFEFC), Tx Rate: 1s */
    J1939_SIGNAL(PGN_DD, 96u, "Fuel Level 1", "%",
                 1000u, J1939_TX_MODE_PERIODIC, 2u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),
    J1939_SIGNAL(PGN_DD, 99u, "Engine Oil Filter Differential Pressure", "kPa",
                 1000u, J1939_TX_MODE_PERIODIC, 4u, 1u, 8u,
                 0.5f, 0.0f, 0.0f, 125.0f),
    J1939_SIGNAL(PGN_DD, 169u, "Cargo Ambient Temperature", "degC",
                 1000u, J1939_TX_MODE_PERIODIC, 5u, 1u, 16u,
                 0.03125f, -273.0f, -273.0f, 1735.0f),
    J1939_SIGNAL(PGN_DD, 38u, "Fuel Level 2", "%",
                 1000u, J1939_TX_MODE_PERIODIC, 7u, 1u, 8u,
                 0.4f, 0.0f, 0.0f, 100.0f),

    /* =====================================================================
     * EVENT-DRIVEN: on change, maximum/fallback 1000 ms
     * ===================================================================== */

    /* PGN_OELC: PGN 64972 (0xFDCC), Tx Rate: On change of switch. Maximum period of 1 second between messages. No greater than 10 messages per second for all switches. */
    J1939_SIGNAL(PGN_OELC, 2876u, "Turn Signal Switch", "State",
                 1000u, J1939_TX_MODE_ON_CHANGE, 2u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_OELC, 2875u, "Hazard Light Switch", "State",
                 1000u, J1939_TX_MODE_ON_CHANGE, 2u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_LC: PGN 65089 (0xFE41), Tx Rate: On change of lamp on/ off state. Maximum period of 1 second between messages. No greater than 10 messages per second for all lights. */
    J1939_SIGNAL(PGN_LC, 2403u, "Running Light Command", "State",
                 1000u, J1939_TX_MODE_ON_CHANGE, 1u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_LC, 2369u, "Right Turn Signal Lights Command", "State",
                 1000u, J1939_TX_MODE_ON_CHANGE, 2u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_LC, 2367u, "Left Turn Signal Lights Command", "State",
                 1000u, J1939_TX_MODE_ON_CHANGE, 2u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* =====================================================================
     * EVENT-DRIVEN: on change, maximum/fallback 5000 ms
     * ===================================================================== */

    /* PGN_RC: PGN 65249 (0xFEE1), Tx Rate: On change of torque/speed points of more than 10% since last transmission, or every 5 s. */
    J1939_SIGNAL(PGN_RC, 901u, "Retarder Type", "State",
                 5000u, J1939_TX_MODE_ON_CHANGE, 1u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_RC, 556u, "Reference Retarder Torque (Retarder Configuration)", "Nm",
                 5000u, J1939_TX_MODE_ON_CHANGE, 17u, 1u, 16u,
                 1.0f, 0.0f, 0.0f, 64255.0f),

    /* PGN_EC: PGN 65251 (0xFEE3), Tx Rate: On change of torque/speed points of more than 10% since last transmission. Alternative: Every 5 s. */
    J1939_SIGNAL(PGN_EC, 544u, "Engine Reference Torque (Engine Configuration)", "Nm",
                 5000u, J1939_TX_MODE_ON_CHANGE, 20u, 1u, 16u,
                 1.0f, 0.0f, 0.0f, 64255.0f),

    /* =====================================================================
     * EVENT-DRIVEN: engine-speed dependent
     * ===================================================================== */

    /* PGN_EEC1: PGN 61444 (0xF004), Tx Rate: engine speed dependent */
    J1939_SIGNAL(PGN_EEC1, 899u, "Engine Torque Mode", "State",
                 0u, J1939_TX_MODE_ENGINE_SPEED_DEPENDENT, 1u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_EEC1, 512u, "Driver's Demand Engine - Percent Torque", "%",
                 0u, J1939_TX_MODE_ENGINE_SPEED_DEPENDENT, 2u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_EEC1, 513u, "Actual Engine - Percent Torque", "%",
                 0u, J1939_TX_MODE_ENGINE_SPEED_DEPENDENT, 3u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),
    J1939_SIGNAL(PGN_EEC1, 190u, "Engine Speed", "rpm",
                 0u, J1939_TX_MODE_ENGINE_SPEED_DEPENDENT, 4u, 1u, 16u,
                 0.125f, 0.0f, 0.0f, 8031.875f),
    J1939_SIGNAL(PGN_EEC1, 1675u, "Engine Starter Mode", "State",
                 0u, J1939_TX_MODE_ENGINE_SPEED_DEPENDENT, 7u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_EEC1, 2432u, "Engine Demand – Percent Torque", "%",
                 0u, J1939_TX_MODE_ENGINE_SPEED_DEPENDENT, 8u, 1u, 8u,
                 1.0f, -125.0f, -125.0f, 125.0f),

    /* =====================================================================
     * ON REQUEST: no fixed period
     * ===================================================================== */

    /* PGN_CVW: PGN 65136 (0xFE70), Tx Rate: On request */
    J1939_SIGNAL(PGN_CVW, 1585u, "Powered Vehicle Weight", "kg",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 16u,
                 10.0f, 0.0f, 0.0f, 642550.0f),
    J1939_SIGNAL(PGN_CVW, 1760u, "Gross Combination Vehicle Weight", "kg",
                 0u, J1939_TX_MODE_ON_REQUEST, 3u, 1u, 16u,
                 10.0f, 0.0f, 0.0f, 642550.0f),

    /* PGN_VP2: PGN 65165 (0xFE8D), Tx Rate: On request */
    J1939_SIGNAL(PGN_VP2, 444u, "Battery Potential / Power Input 2", "Volts",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 16u,
                 0.05f, 0.0f, 0.0f, 3212.75f),

    /* PGN_DM1: PGN 65226 (0xFECA), Tx Rate: On request */
    J1939_SIGNAL(PGN_DM1, 623u, "Red Stop Lamp", "State",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 3u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_DM1, 624u, "Amber Warning Lamp", "State",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 5u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),

    /* PGN_IO: PGN 65244 (0xFEDC), Tx Rate: On request */
    J1939_SIGNAL(PGN_IO, 235u, "Engine Total Idle Hours", "s",
                 0u, J1939_TX_MODE_ON_REQUEST, 5u, 1u, 32u,
                 0.05f, 0.0f, 0.0f, 210554060.75f),

    /* PGN_HOURS: PGN 65253 (0xFEE5), Tx Rate: On request */
    J1939_SIGNAL(PGN_HOURS, 247u, "Engine Total Hours of Operation", "s",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 32u,
                 0.05f, 0.0f, 0.0f, 210554060.75f),

    /* PGN_TD: PGN 65254 (0xFEE6), Tx Rate: On request */
    J1939_SIGNAL(PGN_TD, 959u, "Seconds", "s",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 8u,
                 0.25f, 0.0f, 0.0f, 62.5f),
    J1939_SIGNAL(PGN_TD, 960u, "Minutes", "s",
                 0u, J1939_TX_MODE_ON_REQUEST, 2u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_TD, 961u, "Hours", "s",
                 0u, J1939_TX_MODE_ON_REQUEST, 3u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_TD, 963u, "Month", "month",
                 0u, J1939_TX_MODE_ON_REQUEST, 4u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 250.0f),
    J1939_SIGNAL(PGN_TD, 962u, "Day", "day",
                 0u, J1939_TX_MODE_ON_REQUEST, 5u, 1u, 8u,
                 0.25f, 0.0f, 0.0f, 62.5f),
    J1939_SIGNAL(PGN_TD, 964u, "Year", "yr",
                 0u, J1939_TX_MODE_ON_REQUEST, 6u, 1u, 8u,
                 1.0f, 1985.0f, 1985.0f, 2235.0f),

    /* PGN_LFC: PGN 65257 (0xFEE9), Tx Rate: On request */
    J1939_SIGNAL(PGN_LFC, 182u, "Engine Trip Fuel", "l",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 32u,
                 0.5f, 0.0f, 0.0f, 2105540607.5f),
    J1939_SIGNAL(PGN_LFC, 250u, "Engine Total Fuel Used", "l",
                 0u, J1939_TX_MODE_ON_REQUEST, 5u, 1u, 32u,
                 0.5f, 0.0f, 0.0f, 2105540607.5f),

    /* PGN_VW: PGN 65258 (0xFEEA), Tx Rate: On request */
    J1939_SIGNAL(PGN_VW, 928u, "Axle Location", "State",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 8u,
                 1.0f, 0.0f, 0.0f, 255.0f),
    J1939_SIGNAL(PGN_VW, 582u, "Axle Weight", "kg",
                 0u, J1939_TX_MODE_ON_REQUEST, 2u, 1u, 16u,
                 0.5f, 0.0f, 0.0f, 32127.5f),

    /* PGN_VI: PGN 65260 (0xFEEC), Tx Rate: On request */
    J1939_SIGNAL(PGN_VI, 237u, "Vehicle Identification Number", "ASCII",
                 0u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 1600u,
                 1.0f, 0.0f, 0.0f, 255.0f),

    /* =====================================================================
     * ON REQUEST: 100 ms response/nominal value
     * ===================================================================== */

    /* PGN_EBS21: PGN 65222 (0xFEC6), Tx Rate: On request (100ms) */
    J1939_SIGNAL(PGN_EBS21, 5108u, "Tire Pressure Sufficient", "State",
                 100u, J1939_TX_MODE_ON_REQUEST, 1u, 1u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EBS21, 5109u, "Vehicle Pneum Supply Suff", "State",
                 100u, J1939_TX_MODE_ON_REQUEST, 1u, 7u, 2u,
                 1.0f, 0.0f, 0.0f, 3.0f),
    J1939_SIGNAL(PGN_EBS21, 5110u, "Tire Wheel Ident Pressure 1", "State",
                 100u, J1939_TX_MODE_ON_REQUEST, 2u, 1u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_EBS21, 5111u, "Tire Wheel Ident Pressure 2", "State",
                 100u, J1939_TX_MODE_ON_REQUEST, 2u, 5u, 4u,
                 1.0f, 0.0f, 0.0f, 15.0f),
    J1939_SIGNAL(PGN_EBS21, 241u, "Tire Pressure", "kPa",
                 100u, J1939_TX_MODE_ON_REQUEST, 5u, 1u, 8u,
                 10.0f, 0.0f, 0.0f, 2550.0f),
};

#undef J1939_SIGNAL

enum {
    J1939_SIGNAL_COUNT_COMPILETIME =
        (int)(sizeof(J1939_Signal_Database) / sizeof(J1939_Signal_Database[0]))
};

_Static_assert(J1939_SIGNAL_COUNT_COMPILETIME == J1939_SIGNAL_COUNT_EXPECTED,
               "J1939 master database must contain exactly 179 signals");
_Static_assert(142u + 14u + 23u == J1939_SIGNAL_COUNT_EXPECTED,
               "J1939 transmission-mode counts must cover the database");

const uint16_t J1939_Signal_Count = (uint16_t)J1939_SIGNAL_COUNT_COMPILETIME;
const uint16_t J1939_Periodic_Signal_Count = 142u;
const uint16_t J1939_Event_Signal_Count = 14u;
const uint16_t J1939_On_Request_Signal_Count = 23u;

typedef struct {
    J1939_Signal_Key_t key;
    uint32_t pgn;
    uint32_t spn;
} J1939_Signal_Key_Map_t;

/* Stable UI keys follow the original workbook order, while this C database is
 * sorted by transmission mode/period. Keep this explicit map so a catalog
 * reorder cannot silently change the control protocol. The two legacy MVP
 * signals outside the 40-SPN profile remain available for compatibility. */
static const J1939_Signal_Key_Map_t s_signal_key_map[] = {
    { J1939_SIGNAL_KEY_VDC2_STEERING_ANGLE,     PGN_VDC2, 1807u },
    { J1939_SIGNAL_KEY_VDC2_YAW_RATE,           PGN_VDC2, 1808u },
    { J1939_SIGNAL_KEY_VDC2_LATERAL_ACCEL,      PGN_VDC2, 1809u },
    { J1939_SIGNAL_KEY_VDC2_LONG_ACCEL,         PGN_VDC2, 1810u },
    { J1939_SIGNAL_KEY_TCO1_DRIVER1_STATE,      PGN_TCO1, 1617u },
    { J1939_SIGNAL_KEY_TCO1_OVERSPEED,          PGN_TCO1, 1614u },
    { J1939_SIGNAL_KEY_TCO1_DRIVER2_STATE,      PGN_TCO1, 1618u },
    { J1939_SIGNAL_KEY_TCO1_SYSTEM_EVENT,       PGN_TCO1, 1622u },
    { J1939_SIGNAL_KEY_TCO1_HANDLING_INFO,      PGN_TCO1, 1621u },
    { J1939_SIGNAL_KEY_TCO1_PERFORMANCE,        PGN_TCO1, 1620u },
    { J1939_SIGNAL_KEY_TCO1_DIRECTION,          PGN_TCO1, 1619u },
    { J1939_SIGNAL_KEY_TCO1_OUTPUT_SHAFT_SPEED, PGN_TCO1, 1623u },
    { J1939_SIGNAL_KEY_TCO1_VEHICLE_SPEED,      PGN_TCO1, 1624u },
    { J1939_SIGNAL_KEY_PTODE_ENGAGED,           PGN_PTODE, 3948u },
    { J1939_SIGNAL_KEY_HRW_FRONT_LEFT_SPEED,    PGN_HRW, 1592u },
    { J1939_SIGNAL_KEY_HRW_FRONT_RIGHT_SPEED,   PGN_HRW, 1593u },
    { J1939_SIGNAL_KEY_HRW_REAR_LEFT_SPEED,     PGN_HRW, 1594u },
    { J1939_SIGNAL_KEY_HRW_REAR_RIGHT_SPEED,    PGN_HRW, 1595u },
    { J1939_SIGNAL_KEY_FLI2_RIGHT_STATUS,       PGN_FLI2, 1711u },
    { J1939_SIGNAL_KEY_FLI2_LEFT_STATUS,        PGN_FLI2, 1710u },
    { J1939_SIGNAL_KEY_ETC1_SHIFT_IN_PROCESS,   PGN_ETC1, 574u },
    { J1939_SIGNAL_KEY_ETC1_CLUTCH_SLIP,        PGN_ETC1, 522u },
    { J1939_SIGNAL_KEY_EAC2_AXLE_LOCATION,      PGN_EAC2, 928u },
    { J1939_SIGNAL_KEY_ERC1_TORQUE_MODE,        PGN_ERC1, 900u },
    { J1939_SIGNAL_KEY_ERC1_ACTUAL_TORQUE,      PGN_ERC1, 520u },
    { J1939_SIGNAL_KEY_ERC1_DRIVER_DEMAND,      PGN_ERC1, 1715u },
    { J1939_SIGNAL_KEY_ERC1_MAX_TORQUE,         PGN_ERC1, 1717u },
    { J1939_SIGNAL_KEY_EEC2_LOW_IDLE_SWITCH,    PGN_EEC2, 558u },
    { J1939_SIGNAL_KEY_EEC2_KICKDOWN_SWITCH,    PGN_EEC2, 559u },
    { J1939_SIGNAL_KEY_EEC2_SPEED_LIMIT_STATUS, PGN_EEC2, 1437u },
    { J1939_SIGNAL_KEY_ACCEL_PEDAL_POS,          PGN_EEC2, SPN_ACCEL_PEDAL_POS },
    { J1939_SIGNAL_KEY_EEC2_ENGINE_LOAD,         PGN_EEC2, 92u },
    { J1939_SIGNAL_KEY_EEC2_ACCEL_PEDAL_POS2,    PGN_EEC2, 29u },
    { J1939_SIGNAL_KEY_ENGINE_SPEED,             PGN_EEC1, SPN_ENGINE_SPEED },
    { J1939_SIGNAL_KEY_EBS11_ABS_ACTIVE,         PGN_EBS11, 5113u },
    { J1939_SIGNAL_KEY_EBS11_BRAKE_LIGHT,        PGN_EBS11, 5114u },
    { J1939_SIGNAL_KEY_EBC1_ABS_ACTIVE,          PGN_EBC1, 563u },
    { J1939_SIGNAL_KEY_EBC1_BRAKE_SWITCH,        PGN_EBC1, 1121u },
    { J1939_SIGNAL_KEY_EBC1_PEDAL_POSITION,      PGN_EBC1, 521u },
    { J1939_SIGNAL_KEY_EBC1_CONTROLLER_SA,       PGN_EBC1, 1481u },
    { J1939_SIGNAL_KEY_EBC1_HALT_BRAKE,          PGN_EBC1, 2911u },
    { J1939_SIGNAL_KEY_VEHICLE_SPEED,            PGN_CCVS1, SPN_VEHICLE_SPEED }
};

const J1939_Signal_Key_t J1939_UI_Profile_40_Keys[] = {
    J1939_SIGNAL_KEY_EBS11_ABS_ACTIVE,
    J1939_SIGNAL_KEY_EBS11_BRAKE_LIGHT,
    J1939_SIGNAL_KEY_ETC1_SHIFT_IN_PROCESS,
    J1939_SIGNAL_KEY_ETC1_CLUTCH_SLIP,
    J1939_SIGNAL_KEY_VDC2_STEERING_ANGLE,
    J1939_SIGNAL_KEY_VDC2_YAW_RATE,
    J1939_SIGNAL_KEY_VDC2_LATERAL_ACCEL,
    J1939_SIGNAL_KEY_VDC2_LONG_ACCEL,
    J1939_SIGNAL_KEY_EAC2_AXLE_LOCATION,
    J1939_SIGNAL_KEY_HRW_FRONT_LEFT_SPEED,
    J1939_SIGNAL_KEY_HRW_FRONT_RIGHT_SPEED,
    J1939_SIGNAL_KEY_HRW_REAR_LEFT_SPEED,
    J1939_SIGNAL_KEY_HRW_REAR_RIGHT_SPEED,
    J1939_SIGNAL_KEY_EEC2_LOW_IDLE_SWITCH,
    J1939_SIGNAL_KEY_EEC2_KICKDOWN_SWITCH,
    J1939_SIGNAL_KEY_EEC2_SPEED_LIMIT_STATUS,
    J1939_SIGNAL_KEY_ACCEL_PEDAL_POS,
    J1939_SIGNAL_KEY_EEC2_ENGINE_LOAD,
    J1939_SIGNAL_KEY_EEC2_ACCEL_PEDAL_POS2,
    J1939_SIGNAL_KEY_TCO1_DRIVER1_STATE,
    J1939_SIGNAL_KEY_TCO1_OVERSPEED,
    J1939_SIGNAL_KEY_TCO1_DRIVER2_STATE,
    J1939_SIGNAL_KEY_TCO1_SYSTEM_EVENT,
    J1939_SIGNAL_KEY_TCO1_HANDLING_INFO,
    J1939_SIGNAL_KEY_TCO1_PERFORMANCE,
    J1939_SIGNAL_KEY_TCO1_DIRECTION,
    J1939_SIGNAL_KEY_TCO1_OUTPUT_SHAFT_SPEED,
    J1939_SIGNAL_KEY_TCO1_VEHICLE_SPEED,
    J1939_SIGNAL_KEY_ERC1_TORQUE_MODE,
    J1939_SIGNAL_KEY_ERC1_ACTUAL_TORQUE,
    J1939_SIGNAL_KEY_ERC1_DRIVER_DEMAND,
    J1939_SIGNAL_KEY_ERC1_MAX_TORQUE,
    J1939_SIGNAL_KEY_EBC1_ABS_ACTIVE,
    J1939_SIGNAL_KEY_EBC1_BRAKE_SWITCH,
    J1939_SIGNAL_KEY_EBC1_PEDAL_POSITION,
    J1939_SIGNAL_KEY_EBC1_CONTROLLER_SA,
    J1939_SIGNAL_KEY_EBC1_HALT_BRAKE,
    J1939_SIGNAL_KEY_FLI2_RIGHT_STATUS,
    J1939_SIGNAL_KEY_FLI2_LEFT_STATUS,
    J1939_SIGNAL_KEY_PTODE_ENGAGED
};

const uint16_t J1939_UI_Profile_40_Key_Count =
    (uint16_t)(sizeof(J1939_UI_Profile_40_Keys) /
               sizeof(J1939_UI_Profile_40_Keys[0]));

_Static_assert(sizeof(J1939_UI_Profile_40_Keys) /
                   sizeof(J1939_UI_Profile_40_Keys[0]) ==
                   J1939_UI_PROFILE_40_SIGNAL_COUNT,
               "40-SPN UI profile key count mismatch");

const J1939_Signal_Definition_t* J1939_Find_Signal_By_SPN(uint32_t spn_number)
{
    for (uint16_t i = 0u; i < J1939_Signal_Count; i++) {
        if (J1939_Signal_Database[i].spn == spn_number) {
            return &J1939_Signal_Database[i];
        }
    }
    return NULL;
}

const J1939_Signal_Definition_t* J1939_Find_Unique_Signal_By_SPN(
    uint32_t spn_number,
    uint16_t* match_count)
{
    const J1939_Signal_Definition_t* match = NULL;
    uint16_t count = 0u;

    for (uint16_t i = 0u; i < J1939_Signal_Count; i++) {
        if (J1939_Signal_Database[i].spn == spn_number) {
            if (match == NULL) {
                match = &J1939_Signal_Database[i];
            }
            count++;
        }
    }

    if (match_count != NULL) {
        *match_count = count;
    }
    return (count == 1u) ? match : NULL;
}

const J1939_Signal_Definition_t* J1939_Find_Signal_By_PGN_SPN(uint32_t pgn,
                                                              uint32_t spn_number)
{
    for (uint16_t i = 0u; i < J1939_Signal_Count; i++) {
        if ((J1939_Signal_Database[i].pgn == pgn) &&
            (J1939_Signal_Database[i].spn == spn_number)) {
            return &J1939_Signal_Database[i];
        }
    }
    return NULL;
}

bool J1939_Get_Signal_Id(const J1939_Signal_Definition_t* signal_def,
                         J1939_Signal_Id_t* signal_id)
{
    if ((signal_def == NULL) || (signal_id == NULL)) {
        return false;
    }

    for (J1939_Signal_Id_t i = 0u; i < J1939_Signal_Count; i++) {
        if (signal_def == &J1939_Signal_Database[i]) {
            *signal_id = i;
            return true;
        }
    }
    return false;
}

const J1939_Signal_Definition_t* J1939_Find_Signal_By_Id(
    J1939_Signal_Id_t signal_id)
{
    return (signal_id < J1939_Signal_Count)
        ? &J1939_Signal_Database[signal_id]
        : NULL;
}

const J1939_Signal_Definition_t* J1939_Find_Signal_By_Key(
    J1939_Signal_Key_t signal_key)
{
    for (uint16_t i = 0u;
         i < (uint16_t)(sizeof(s_signal_key_map) / sizeof(s_signal_key_map[0]));
         i++) {
        if (s_signal_key_map[i].key == signal_key) {
            return J1939_Find_Signal_By_PGN_SPN(s_signal_key_map[i].pgn,
                                                s_signal_key_map[i].spn);
        }
    }
    return NULL;
}

bool J1939_Get_Signal_Key(J1939_Signal_Id_t signal_id,
                          J1939_Signal_Key_t* signal_key)
{
    const J1939_Signal_Definition_t* def = J1939_Find_Signal_By_Id(signal_id);
    if ((def == NULL) || (signal_key == NULL)) {
        return false;
    }

    for (uint16_t i = 0u;
         i < (uint16_t)(sizeof(s_signal_key_map) / sizeof(s_signal_key_map[0]));
         i++) {
        if ((s_signal_key_map[i].pgn == def->pgn) &&
            (s_signal_key_map[i].spn == def->spn)) {
            *signal_key = s_signal_key_map[i].key;
            return true;
        }
    }
    return false;
}
