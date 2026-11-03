#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "j1939_encode_decode.h"
#include "j1939_signal_definitions_legacy.h"

static void test_database_identity(void)
{
    uint16_t count = 0u;

    assert(J1939_Signal_Count == J1939_SIGNAL_COUNT_EXPECTED);
    assert(J1939_Find_Unique_Signal_By_SPN(SPN_ENGINE_SPEED, &count) != NULL);
    assert(count == 1u);

    assert(J1939_Find_Unique_Signal_By_SPN(928u, &count) == NULL);
    assert(count == 2u);
    assert(J1939_Find_Signal_By_PGN_SPN(PGN_EAC2, 928u) != NULL);
    assert(J1939_Find_Signal_By_PGN_SPN(PGN_VW, 928u) != NULL);

    assert(J1939_Find_Unique_Signal_By_SPN(1807u, &count) == NULL);
    assert(count == 2u);
    assert(J1939_Find_Signal_By_PGN_SPN(PGN_VDC2, 1807u) != NULL);
    assert(J1939_Find_Signal_By_PGN_SPN(PGN_SWA, 1807u) != NULL);
}

static void test_sized_scalar_codec(void)
{
    const J1939_Signal_Definition_t byte_20_u16 = {
        .pgn = 1u,
        .spn = 1u,
        .name = "test",
        .unit = "raw",
        .start_byte = 20u,
        .start_bit = 1u,
        .num_bits = 16u,
        .resolution = 1.0f,
        .offset = 0.0f,
        .min_physical = 0.0f,
        .max_physical = 65535.0f
    };
    uint8_t payload[21];
    float decoded = 0.0f;

    memset(payload, 0xFF, sizeof(payload));
    assert(!J1939_Is_Signal_Scalar_In_Buffer(&byte_20_u16, 20u));
    assert(J1939_Is_Signal_Scalar_In_Buffer(&byte_20_u16, 21u));
    assert(!J1939_Encode_Signal_Sized(&byte_20_u16, 4660.0f,
                                      payload, 20u));
    assert(payload[19] == 0xFFu && payload[20] == 0xFFu);

    assert(J1939_Encode_Signal_Sized(&byte_20_u16, 4660.0f,
                                     payload, sizeof(payload)));
    assert(payload[19] == 0x34u && payload[20] == 0x12u);
    assert(J1939_Decode_Signal_Sized(&byte_20_u16, payload,
                                     sizeof(payload), &decoded));
    assert(decoded == 4660.0f);
}

static void test_bit_boundary(void)
{
    const J1939_Signal_Definition_t crossing_bit = {
        .pgn = 1u,
        .spn = 2u,
        .name = "crossing",
        .unit = "raw",
        .start_byte = 8u,
        .start_bit = 8u,
        .num_bits = 2u,
        .resolution = 1.0f,
        .offset = 0.0f,
        .min_physical = 0.0f,
        .max_physical = 3.0f
    };
    uint8_t payload[9] = {0u};
    float decoded = 0.0f;

    assert(!J1939_Is_Signal_8Byte_Scalar(&crossing_bit));
    assert(!J1939_Encode_Signal_Sized(&crossing_bit, 3.0f, payload, 8u));
    assert(J1939_Encode_Signal_Sized(&crossing_bit, 3.0f,
                                     payload, sizeof(payload)));
    assert(payload[7] == 0x80u && payload[8] == 0x01u);
    assert(J1939_Decode_Signal_Sized(&crossing_bit, payload,
                                     sizeof(payload), &decoded));
    assert(decoded == 3.0f);
}

static void test_aligned_nonstandard_width(void)
{
    const J1939_Signal_Definition_t aligned_12_bit = {
        .pgn = 1u,
        .spn = 3u,
        .name = "aligned-12",
        .unit = "raw",
        .start_byte = 1u,
        .start_bit = 1u,
        .num_bits = 12u,
        .resolution = 1.0f,
        .offset = 0.0f,
        .min_physical = 0.0f,
        .max_physical = 4095.0f
    };
    uint8_t payload[2] = {0u, 0u};
    float decoded = 0.0f;

    assert(J1939_Encode_Signal_Sized(&aligned_12_bit, 2748.0f,
                                     payload, sizeof(payload)));
    assert(payload[0] == 0xBCu && payload[1] == 0x0Au);
    assert(J1939_Decode_Signal_Sized(&aligned_12_bit, payload,
                                     sizeof(payload), &decoded));
    assert(decoded == 2748.0f);
}

static void test_classic_frame_wrapper_and_vin(void)
{
    const J1939_Signal_Definition_t* engine_speed =
        J1939_Find_Signal_By_PGN_SPN(PGN_EEC1, SPN_ENGINE_SPEED);
    const J1939_Signal_Definition_t* vin =
        J1939_Find_Signal_By_PGN_SPN(PGN_VI, 237u);
    uint8_t payload[8];

    assert(engine_speed != NULL);
    memset(payload, 0xFF, sizeof(payload));
    assert(J1939_Encode_Signal(engine_speed, 1500.0f, payload));
    assert(payload[3] == 0xE0u && payload[4] == 0x2Eu);
    assert(J1939_Decode_Signal(engine_speed, payload) == 1500.0f);

    assert(vin != NULL);
    assert(!J1939_Is_Signal_Scalar_In_Buffer(vin, 200u));
}

int main(void)
{
    test_database_identity();
    test_sized_scalar_codec();
    test_bit_boundary();
    test_aligned_nonstandard_width();
    test_classic_frame_wrapper_and_vin();
    puts("Step 1A host tests: PASS");
    return 0;
}
