/*
 * =============================================================================
 * FILE : j1939_value_store.c
 * WHAT : Double-banked raw values and active bitmaps for all catalog records.
 * =============================================================================
 */

#include "j1939_value_store.h"
#include <stddef.h>
#include <string.h>

#define VS_BANKS  2u

typedef enum {
    VS_TXN_IDLE = 0,
    VS_TXN_OPEN,
    VS_TXN_REJECTED
} Vs_Txn_State_t;

typedef struct {
    uint32_t raw_value[VS_BANKS][J1939_CATALOG_SIGNAL_COUNT];
    uint8_t  active_mask[VS_BANKS][J1939_CATALOG_ACTIVE_MASK_BYTES];
    uint16_t active_count[VS_BANKS];
    uint16_t generation;
    uint8_t  active_bank;
    uint8_t  txn_state;
} J1939_Value_Banks_t;

static J1939_Value_Banks_t s_vs;

static uint8_t prv_staging_bank(void)
{
    return (uint8_t)(s_vs.active_bank ^ 1u);
}

static bool prv_mask_test(const uint8_t* mask, uint16_t index)
{
    return (mask[index / 8u] & (uint8_t)(1u << (index % 8u))) != 0u;
}

static void prv_mask_set(uint8_t* mask, uint16_t index, bool on)
{
    const uint8_t bit = (uint8_t)(1u << (index % 8u));
    if (on) {
        mask[index / 8u] |= bit;
    } else {
        mask[index / 8u] &= (uint8_t)~bit;
    }
}

static J1939_Value_Store_Status_t prv_check_index(uint16_t index,
                                                  uint32_t raw_value)
{
    const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[index];
    if ((sig->flags & J1939_CAT_SIG_BLOB) != 0u) {
        return J1939_VALUE_STORE_UNSUPPORTED_SIGNAL;
    }
    if ((raw_value < sig->raw_min) || (raw_value > sig->raw_max)) {
        return J1939_VALUE_STORE_VALUE_OUT_OF_RANGE;
    }
    return J1939_VALUE_STORE_OK;
}

/* Rejection poisons an open transaction so it cannot commit partially. */
static J1939_Value_Store_Status_t prv_reject(J1939_Value_Store_Status_t status)
{
    if (s_vs.txn_state == VS_TXN_OPEN) {
        s_vs.txn_state = VS_TXN_REJECTED;
    }
    return status;
}

static J1939_Value_Store_Status_t prv_txn_ready(void)
{
    if (s_vs.txn_state == VS_TXN_IDLE) {
        return J1939_VALUE_STORE_NO_TRANSACTION;
    }
    if (s_vs.txn_state == VS_TXN_REJECTED) {
        return J1939_VALUE_STORE_TRANSACTION_REJECTED;
    }
    return J1939_VALUE_STORE_OK;
}

void J1939_Value_Store_Init(void)
{
    memset(&s_vs, 0, sizeof(s_vs));
}

J1939_Value_Store_Status_t J1939_Value_Store_Check_Raw(uint16_t signal_key,
                                                       uint32_t raw_value)
{
    const uint16_t index = J1939_Catalog_Index_From_Key(signal_key);
    if (index == J1939_CATALOG_INDEX_INVALID) {
        return J1939_VALUE_STORE_UNKNOWN_KEY;
    }
    return prv_check_index(index, raw_value);
}

J1939_Value_Store_Status_t J1939_Value_Store_Begin(J1939_Value_Store_Begin_t start)
{
    if (s_vs.txn_state != VS_TXN_IDLE) {
        return J1939_VALUE_STORE_TRANSACTION_OPEN;
    }

    const uint8_t staging = prv_staging_bank();
    if (start == J1939_VALUE_STORE_BEGIN_COPY_ACTIVE) {
        memcpy(s_vs.raw_value[staging], s_vs.raw_value[s_vs.active_bank],
               sizeof(s_vs.raw_value[0]));
        memcpy(s_vs.active_mask[staging], s_vs.active_mask[s_vs.active_bank],
               sizeof(s_vs.active_mask[0]));
    } else {
        memset(s_vs.raw_value[staging], 0, sizeof(s_vs.raw_value[0]));
        memset(s_vs.active_mask[staging], 0, sizeof(s_vs.active_mask[0]));
    }
    s_vs.txn_state = VS_TXN_OPEN;
    return J1939_VALUE_STORE_OK;
}

J1939_Value_Store_Status_t J1939_Value_Store_Set_Raw(uint16_t signal_key,
                                                     uint32_t raw_value)
{
    J1939_Value_Store_Status_t status = prv_txn_ready();
    if (status != J1939_VALUE_STORE_OK) {
        return status;
    }

    status = J1939_Value_Store_Check_Raw(signal_key, raw_value);
    if (status != J1939_VALUE_STORE_OK) {
        return prv_reject(status);
    }

    const uint8_t staging = prv_staging_bank();
    const uint16_t index = J1939_Catalog_Index_From_Key(signal_key);
    s_vs.raw_value[staging][index] = raw_value;
    prv_mask_set(s_vs.active_mask[staging], index, true);
    return J1939_VALUE_STORE_OK;
}

J1939_Value_Store_Status_t J1939_Value_Store_Remove(uint16_t signal_key)
{
    const J1939_Value_Store_Status_t status = prv_txn_ready();
    if (status != J1939_VALUE_STORE_OK) {
        return status;
    }

    const uint16_t index = J1939_Catalog_Index_From_Key(signal_key);
    if (index == J1939_CATALOG_INDEX_INVALID) {
        return prv_reject(J1939_VALUE_STORE_UNKNOWN_KEY);
    }
    prv_mask_set(s_vs.active_mask[prv_staging_bank()], index, false);
    return J1939_VALUE_STORE_OK;
}

J1939_Value_Store_Status_t J1939_Value_Store_Commit(uint16_t* out_generation)
{
    J1939_Value_Store_Status_t status = prv_txn_ready();

    /* Whole-bank validation: commit never relies on earlier per-write checks. */
    const uint8_t staging = prv_staging_bank();
    uint16_t count = 0u;
    for (uint16_t i = 0u; (status == J1939_VALUE_STORE_OK) &&
                          (i < J1939_CATALOG_SIGNAL_COUNT); i++) {
        if (prv_mask_test(s_vs.active_mask[staging], i)) {
            status = prv_check_index(i, s_vs.raw_value[staging][i]);
            count++;
        }
    }
    if ((status == J1939_VALUE_STORE_OK) && ((J1939_CATALOG_SIGNAL_COUNT % 8u) != 0u)) {
        const uint8_t tail = s_vs.active_mask[staging][J1939_CATALOG_ACTIVE_MASK_BYTES - 1u];
        if ((tail >> (J1939_CATALOG_SIGNAL_COUNT % 8u)) != 0u) {
            status = J1939_VALUE_STORE_UNKNOWN_KEY;
        }
    }

    if (status == J1939_VALUE_STORE_OK) {
        s_vs.active_count[staging] = count;
        s_vs.active_bank = staging;
        s_vs.generation = (uint16_t)(s_vs.generation + 1u);
        if (s_vs.generation == 0u) {
            s_vs.generation = 1u;
        }
    }
    /* A failed commit discards the staging bank. */
    s_vs.txn_state = VS_TXN_IDLE;
    if (out_generation != NULL) {
        *out_generation = s_vs.generation;
    }
    return status;
}

void J1939_Value_Store_Abort(void)
{
    s_vs.txn_state = VS_TXN_IDLE;
}

bool J1939_Value_Store_In_Transaction(void)
{
    return s_vs.txn_state != VS_TXN_IDLE;
}

bool J1939_Value_Store_Is_Active(uint16_t catalog_index)
{
    return (catalog_index < J1939_CATALOG_SIGNAL_COUNT) &&
           prv_mask_test(s_vs.active_mask[s_vs.active_bank], catalog_index);
}

bool J1939_Value_Store_Get_Raw(uint16_t catalog_index, uint32_t* out_raw)
{
    if ((out_raw == NULL) || !J1939_Value_Store_Is_Active(catalog_index)) {
        return false;
    }
    *out_raw = s_vs.raw_value[s_vs.active_bank][catalog_index];
    return true;
}

uint16_t J1939_Value_Store_Get_Active_Count(void)
{
    return s_vs.active_count[s_vs.active_bank];
}

uint16_t J1939_Value_Store_Get_Generation(void)
{
    return s_vs.generation;
}
