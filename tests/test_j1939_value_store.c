/*
 * Phase 2 host test: compact value store (double bank, atomic commit,
 * generation) and the raw field codec.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "j1939_signal_definitions.h"
#include "j1939_catalog_test_vectors_generated.h"
#include "j1939_encode_decode.h"
#include "j1939_value_store.h"

#define SCALAR_RECORD_COUNT  (J1939_CATALOG_SIGNAL_COUNT - 1u)  /* all but VIN */

static uint32_t mid_raw(const J1939_Catalog_Signal_t* s)
{
    return s->raw_min + (s->raw_max - s->raw_min) / 2u;
}

static bool is_scalar(const J1939_Catalog_Signal_t* s)
{
    return (s->flags & J1939_CAT_SIG_BLOB) == 0u;
}

/* Stage every scalar record with value(key) and commit. */
static void configure_all(uint32_t (*value)(const J1939_Catalog_Signal_t*))
{
    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_EMPTY) == J1939_VALUE_STORE_OK);
    for (uint16_t key = 1u; key <= J1939_CATALOG_KEY_MAX; key++) {
        const J1939_Catalog_Signal_t* s =
            &J1939_Catalog_Signals[J1939_Catalog_Index_From_Key(key)];
        if (is_scalar(s)) {
            assert(J1939_Value_Store_Set_Raw(key, value(s)) == J1939_VALUE_STORE_OK);
        }
    }
    uint16_t generation = 0u;
    assert(J1939_Value_Store_Commit(&generation) == J1939_VALUE_STORE_OK);
    assert(generation == J1939_Value_Store_Get_Generation());
}

static uint32_t raw_min_of(const J1939_Catalog_Signal_t* s) { return s->raw_min; }
static uint32_t raw_max_of(const J1939_Catalog_Signal_t* s) { return s->raw_max; }

static void assert_active_values(uint32_t (*value)(const J1939_Catalog_Signal_t*))
{
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[i];
        uint32_t raw = 0xDEADBEEFu;
        if (is_scalar(s)) {
            assert(J1939_Value_Store_Get_Raw(i, &raw));
            assert(raw == value(s));
        } else {
            assert(!J1939_Value_Store_Is_Active(i));
            assert(!J1939_Value_Store_Get_Raw(i, &raw));
        }
    }
}

static void test_configure_all_scalar_records(void)
{
    J1939_Value_Store_Init();
    assert(J1939_Value_Store_Get_Generation() == 0u);
    assert(J1939_Value_Store_Get_Active_Count() == 0u);

    configure_all(mid_raw);
    assert(J1939_Value_Store_Get_Generation() == 1u);
    assert(J1939_Value_Store_Get_Active_Count() == SCALAR_RECORD_COUNT);
    assert_active_values(mid_raw);

    /* The VIN blob is not a scalar record; WRITE_BLOB handles it later. */
    assert(J1939_Value_Store_Check_Raw(J1939_KEY_VI_SPN237, 0x41u) ==
           J1939_VALUE_STORE_UNSUPPORTED_SIGNAL);
}

static void test_atomic_bank_swap(void)
{
    J1939_Value_Store_Init();
    configure_all(raw_min_of);
    const uint16_t generation = J1939_Value_Store_Get_Generation();

    /* Stage a complete second configuration; readers keep the old one. */
    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_COPY_ACTIVE) ==
           J1939_VALUE_STORE_OK);
    for (uint16_t key = 1u; key <= J1939_CATALOG_KEY_MAX; key++) {
        const J1939_Catalog_Signal_t* s =
            &J1939_Catalog_Signals[J1939_Catalog_Index_From_Key(key)];
        if (is_scalar(s)) {
            assert(J1939_Value_Store_Set_Raw(key, s->raw_max) == J1939_VALUE_STORE_OK);
            assert_active_values(raw_min_of);   /* checked after every write */
        }
    }
    assert(J1939_Value_Store_Get_Generation() == generation);

    assert(J1939_Value_Store_Commit(NULL) == J1939_VALUE_STORE_OK);
    assert(J1939_Value_Store_Get_Generation() == (uint16_t)(generation + 1u));
    assert_active_values(raw_max_of);

    /* Abort discards a staged configuration completely. */
    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_EMPTY) == J1939_VALUE_STORE_OK);
    assert(J1939_Value_Store_Set_Raw(J1939_KEY_EEC1_SPN190, 1u) == J1939_VALUE_STORE_OK);
    J1939_Value_Store_Abort();
    assert(J1939_Value_Store_Get_Generation() == (uint16_t)(generation + 1u));
    assert(J1939_Value_Store_Get_Active_Count() == SCALAR_RECORD_COUNT);
    assert_active_values(raw_max_of);

    /* COPY_ACTIVE keeps untouched records; Remove deactivates one. */
    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_COPY_ACTIVE) ==
           J1939_VALUE_STORE_OK);
    assert(J1939_Value_Store_Remove(J1939_KEY_EEC1_SPN190) == J1939_VALUE_STORE_OK);
    assert(J1939_Value_Store_Commit(NULL) == J1939_VALUE_STORE_OK);
    assert(J1939_Value_Store_Get_Active_Count() == SCALAR_RECORD_COUNT - 1u);
    assert(!J1939_Value_Store_Is_Active(J1939_Catalog_Index_From_Key(J1939_KEY_EEC1_SPN190)));
    assert(J1939_Value_Store_Is_Active(J1939_Catalog_Index_From_Key(J1939_KEY_CCVS1_SPN84)));
}

static void assert_rejected_transaction(uint16_t key, uint32_t raw,
                                        J1939_Value_Store_Status_t expected)
{
    const uint16_t generation = J1939_Value_Store_Get_Generation();

    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_COPY_ACTIVE) ==
           J1939_VALUE_STORE_OK);
    /* A valid write before and after the bad one must not survive either. */
    assert(J1939_Value_Store_Set_Raw(J1939_KEY_EEC1_SPN190, 0u) == J1939_VALUE_STORE_OK);
    assert(J1939_Value_Store_Set_Raw(key, raw) == expected);
    assert(J1939_Value_Store_Set_Raw(J1939_KEY_CCVS1_SPN84, 0u) ==
           J1939_VALUE_STORE_TRANSACTION_REJECTED);

    uint16_t reported = 0u;
    assert(J1939_Value_Store_Commit(&reported) == J1939_VALUE_STORE_TRANSACTION_REJECTED);
    assert(reported == generation);
    assert(J1939_Value_Store_Get_Generation() == generation);
    assert(!J1939_Value_Store_In_Transaction());
    assert_active_values(mid_raw);
}

static void test_invalid_key_or_value_keeps_generation(void)
{
    J1939_Value_Store_Init();
    configure_all(mid_raw);

    assert_rejected_transaction(0u, 0u, J1939_VALUE_STORE_UNKNOWN_KEY);
    assert_rejected_transaction(J1939_CATALOG_KEY_MAX + 1u, 0u, J1939_VALUE_STORE_UNKNOWN_KEY);
    assert_rejected_transaction(0xFFFFu, 0u, J1939_VALUE_STORE_UNKNOWN_KEY);
    assert_rejected_transaction(J1939_KEY_EEC1_SPN190, 64256u,
                                J1939_VALUE_STORE_VALUE_OUT_OF_RANGE);
    assert_rejected_transaction(J1939_KEY_VI_SPN237, 0x41u,
                                J1939_VALUE_STORE_UNSUPPORTED_SIGNAL);

    /* Every record rejects raw_max + 1 (operational/J1939 reserved range). */
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[i];
        if (is_scalar(s) && (s->raw_max < 0xFFFFFFFFu)) {
            assert(J1939_Value_Store_Check_Raw(s->signal_key, s->raw_max + 1u) ==
                   J1939_VALUE_STORE_VALUE_OUT_OF_RANGE);
        }
        if (is_scalar(s)) {
            assert(J1939_Value_Store_Check_Raw(s->signal_key, s->raw_max) ==
                   J1939_VALUE_STORE_OK);
            assert(J1939_Value_Store_Check_Raw(s->signal_key, s->raw_min) ==
                   J1939_VALUE_STORE_OK);
        }
    }

    /* Protocol misuse. */
    assert(J1939_Value_Store_Set_Raw(J1939_KEY_EEC1_SPN190, 1u) ==
           J1939_VALUE_STORE_NO_TRANSACTION);
    assert(J1939_Value_Store_Commit(NULL) == J1939_VALUE_STORE_NO_TRANSACTION);
    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_EMPTY) == J1939_VALUE_STORE_OK);
    assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_EMPTY) ==
           J1939_VALUE_STORE_TRANSACTION_OPEN);
    J1939_Value_Store_Abort();
    assert_active_values(mid_raw);
}

static void test_generation_wraps_to_nonzero(void)
{
    J1939_Value_Store_Init();
    for (uint32_t i = 0u; i < 0x10000u; i++) {
        assert(J1939_Value_Store_Begin(J1939_VALUE_STORE_BEGIN_COPY_ACTIVE) ==
               J1939_VALUE_STORE_OK);
        assert(J1939_Value_Store_Commit(NULL) == J1939_VALUE_STORE_OK);
        assert(J1939_Value_Store_Get_Generation() != 0u);
    }
    assert(J1939_Value_Store_Get_Generation() == 1u);
}

static void check_round_trip(const J1939_Catalog_Signal_t* s, uint16_t length, uint32_t raw)
{
    uint8_t ones[J1939_CATALOG_MAX_PAYLOAD_BYTES];
    uint8_t zeros[J1939_CATALOG_MAX_PAYLOAD_BYTES];
    uint32_t back = 0u;

    memset(ones, 0xFF, sizeof(ones));
    memset(zeros, 0x00, sizeof(zeros));
    assert(J1939_Pack_Raw(ones, length, s->start_bit, (uint8_t)s->bit_length, raw));
    assert(J1939_Pack_Raw(zeros, length, s->start_bit, (uint8_t)s->bit_length, raw));
    assert(J1939_Unpack_Raw(ones, length, s->start_bit, (uint8_t)s->bit_length, &back));
    assert(back == raw);
    assert(J1939_Unpack_Raw(zeros, length, s->start_bit, (uint8_t)s->bit_length, &back));
    assert(back == raw);

    /* Bits outside the field are untouched in both backgrounds. */
    for (uint16_t bit = 0u; bit < length * 8u; bit++) {
        if ((bit >= s->start_bit) && (bit < s->start_bit + s->bit_length)) {
            continue;
        }
        assert((ones[bit / 8u] >> (bit % 8u)) & 1u);
        assert(((zeros[bit / 8u] >> (bit % 8u)) & 1u) == 0u);
    }
}

static void test_raw_packing_round_trip(void)
{
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[i];
        if (!is_scalar(s)) {
            continue;
        }
        const uint16_t length = J1939_Catalog_PGNs[s->pgn_index].payload_length;
        const uint32_t field_mask = (s->bit_length >= 32u)
            ? 0xFFFFFFFFu : ((1u << s->bit_length) - 1u);

        check_round_trip(s, length, s->raw_min);
        check_round_trip(s, length, mid_raw(s));
        check_round_trip(s, length, s->raw_max);
        check_round_trip(s, length, 0x55555555u & field_mask);
        check_round_trip(s, length, 0xAAAAAAAAu & field_mask);

        /* Boundary and width violations are rejected without writing. */
        uint8_t buf[J1939_CATALOG_MAX_PAYLOAD_BYTES];
        memset(buf, 0x5Au, sizeof(buf));
        if (s->bit_length < 32u) {
            assert(!J1939_Pack_Raw(buf, length, s->start_bit, (uint8_t)s->bit_length,
                                   field_mask + 1u));
        }
        assert(!J1939_Pack_Raw(buf, (uint16_t)((s->start_bit + s->bit_length - 1u) / 8u),
                               s->start_bit, (uint8_t)s->bit_length, 0u));
        for (uint16_t b = 0u; b < sizeof(buf); b++) {
            assert(buf[b] == 0x5Au);
        }
    }

    uint8_t buf[8];
    uint32_t raw;
    assert(!J1939_Pack_Raw(NULL, 8u, 0u, 8u, 0u));
    assert(!J1939_Pack_Raw(buf, 8u, 0u, 0u, 0u));
    assert(!J1939_Pack_Raw(buf, 8u, 0u, 33u, 0u));
    assert(!J1939_Unpack_Raw(buf, 8u, 0u, 8u, NULL));
    assert(!J1939_Unpack_Raw(buf, 8u, 60u, 8u, &raw));

    /* Generated vectors agree with the firmware packer. */
    for (uint16_t v = 0u; v < J1939_SIGNAL_VECTOR_COUNT; v++) {
        const J1939_Signal_Vector_t* vec = &J1939_Signal_Vectors[v];
        const J1939_Catalog_Signal_t* s =
            &J1939_Catalog_Signals[J1939_Catalog_Index_From_Key(vec->signal_key)];
        uint8_t big[J1939_VECTOR_MAX_PAYLOAD];
        memset(big, 0xFF, sizeof(big));
        assert(J1939_Pack_Raw(big, vec->payload_length, s->start_bit,
                              (uint8_t)s->bit_length, vec->raw));
        assert(memcmp(big, vec->payload, vec->payload_length) == 0);
    }
}

/* What the Phase 3 scheduler will do: one payload per PGN from the store. */
static void test_pgn_payloads_from_store(void)
{
    J1939_Value_Store_Init();
    configure_all(mid_raw);

    for (uint16_t v = 0u; v < J1939_PGN_VECTOR_COUNT; v++) {
        const J1939_PGN_Vector_t* vec = &J1939_PGN_Vectors[v];
        const J1939_Catalog_PGN_t* p =
            &J1939_Catalog_PGNs[J1939_Catalog_Find_PGN_Index(vec->pgn)];
        uint8_t payload[J1939_VECTOR_MAX_PAYLOAD];

        memset(payload, 0xFF, sizeof(payload));
        for (uint16_t i = p->first_signal; i < p->first_signal + p->signal_count; i++) {
            uint32_t raw;
            assert(J1939_Value_Store_Get_Raw(i, &raw));
            assert(J1939_Pack_Raw(payload, p->payload_length,
                                  J1939_Catalog_Signals[i].start_bit,
                                  (uint8_t)J1939_Catalog_Signals[i].bit_length, raw));
        }
        assert(memcmp(payload, vec->payload, p->payload_length) == 0);
    }
}

int main(void)
{
    test_configure_all_scalar_records();
    test_atomic_bank_swap();
    test_invalid_key_or_value_keeps_generation();
    test_generation_wraps_to_nonzero();
    test_raw_packing_round_trip();
    test_pgn_payloads_from_store();
    printf("Phase 2 value store host tests: PASS (%u scalar records)\n",
           (unsigned)SCALAR_RECORD_COUNT);
    return 0;
}
