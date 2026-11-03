/*
 * =============================================================================
 * FILE : j1939_value_store.c
 * WHAT : Double-banked raw values, blob slot and active bitmaps for all
 *        catalog records, plus the active configuration CRC.
 * =============================================================================
 */

#include "j1939_value_store.h"
#include "j1939_crc32.h"
#include <stddef.h>
#include <string.h>

#define VS_BANKS  2u
#define VS_BLOB_MASK_BYTES  ((J1939_VALUE_STORE_BLOB_MAX_BYTES + 7u) / 8u)

typedef enum {
    VS_TXN_IDLE = 0,
    VS_TXN_OPEN,
    VS_TXN_REJECTED
} Vs_Txn_State_t;

typedef struct {
    uint32_t raw_value[VS_BANKS][J1939_CATALOG_SIGNAL_COUNT];
    uint32_t config_crc[VS_BANKS];
    uint8_t  blob[VS_BANKS][J1939_VALUE_STORE_BLOB_MAX_BYTES];
    uint16_t blob_length[VS_BANKS];
    uint16_t blob_index[VS_BANKS];          /* catalog index or INDEX_INVALID */
    uint8_t  active_mask[VS_BANKS][J1939_CATALOG_ACTIVE_MASK_BYTES];
    uint16_t active_count[VS_BANKS];
    uint16_t generation;
    uint8_t  active_bank;
    uint8_t  txn_state;
    /* Staging-only blob progress for the open transaction. */
    uint8_t  blob_written[VS_BLOB_MASK_BYTES];
    uint16_t blob_written_count;
    bool     blob_touched;
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

static bool prv_is_blob(uint16_t index)
{
    return (J1939_Catalog_Signals[index].flags & J1939_CAT_SIG_BLOB) != 0u;
}

static uint16_t prv_blob_capacity(uint16_t index)
{
    const uint16_t bytes = (uint16_t)(J1939_Catalog_Signals[index].bit_length / 8u);
    return (bytes < J1939_VALUE_STORE_BLOB_MAX_BYTES) ? bytes
                                                      : J1939_VALUE_STORE_BLOB_MAX_BYTES;
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

static J1939_Value_Store_Status_t prv_check_blob(uint8_t bank, uint16_t index)
{
    const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[index];
    const uint16_t length = s_vs.blob_length[bank];
    if ((s_vs.blob_index[bank] != index) || (length == 0u) ||
        (length > prv_blob_capacity(index))) {
        return J1939_VALUE_STORE_BLOB_INCOMPLETE;
    }
    for (uint16_t i = 0u; i < length; i++) {
        if ((s_vs.blob[bank][i] < sig->raw_min) ||
            (s_vs.blob[bank][i] > sig->raw_max)) {
            return J1939_VALUE_STORE_VALUE_OUT_OF_RANGE;
        }
    }
    return J1939_VALUE_STORE_OK;
}

static void prv_clear_blob(uint8_t bank)
{
    memset(s_vs.blob[bank], 0, sizeof(s_vs.blob[0]));
    s_vs.blob_length[bank] = 0u;
    s_vs.blob_index[bank] = J1939_CATALOG_INDEX_INVALID;
}

static uint32_t prv_config_crc(uint8_t bank)
{
    static const uint8_t k_magic[8] = { 'J', '1', '9', '3', '9', 'C', 'F', 'G' };
    uint32_t crc = J1939_Crc32_Update(J1939_CRC32_INIT, k_magic, sizeof(k_magic));
    crc = J1939_Crc32_Update_U32_LE(crc, J1939_CATALOG_CRC32);
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        if (!prv_mask_test(s_vs.active_mask[bank], i)) {
            continue;
        }
        crc = J1939_Crc32_Update_U16_LE(crc, J1939_Catalog_Signals[i].signal_key);
        if (prv_is_blob(i)) {
            crc = J1939_Crc32_Update_U16_LE(crc, s_vs.blob_length[bank]);
            crc = J1939_Crc32_Update(crc, s_vs.blob[bank], s_vs.blob_length[bank]);
        } else {
            crc = J1939_Crc32_Update_U32_LE(crc, s_vs.raw_value[bank][i]);
        }
    }
    return J1939_Crc32_Final(crc);
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
    for (uint8_t bank = 0u; bank < VS_BANKS; bank++) {
        prv_clear_blob(bank);
        s_vs.config_crc[bank] = prv_config_crc(bank);
    }
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
    const uint8_t active = s_vs.active_bank;
    if (start == J1939_VALUE_STORE_BEGIN_COPY_ACTIVE) {
        memcpy(s_vs.raw_value[staging], s_vs.raw_value[active],
               sizeof(s_vs.raw_value[0]));
        memcpy(s_vs.active_mask[staging], s_vs.active_mask[active],
               sizeof(s_vs.active_mask[0]));
        memcpy(s_vs.blob[staging], s_vs.blob[active], sizeof(s_vs.blob[0]));
        s_vs.blob_length[staging] = s_vs.blob_length[active];
        s_vs.blob_index[staging] = s_vs.blob_index[active];
    } else {
        memset(s_vs.raw_value[staging], 0, sizeof(s_vs.raw_value[0]));
        memset(s_vs.active_mask[staging], 0, sizeof(s_vs.active_mask[0]));
        prv_clear_blob(staging);
    }
    memset(s_vs.blob_written, 0, sizeof(s_vs.blob_written));
    s_vs.blob_written_count = 0u;
    s_vs.blob_touched = false;
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

J1939_Value_Store_Status_t J1939_Value_Store_Write_Blob(uint16_t signal_key,
                                                        uint16_t total_length,
                                                        uint16_t offset,
                                                        const uint8_t* data,
                                                        uint16_t count)
{
    const J1939_Value_Store_Status_t status = prv_txn_ready();
    if (status != J1939_VALUE_STORE_OK) {
        return status;
    }

    const uint16_t index = J1939_Catalog_Index_From_Key(signal_key);
    if (index == J1939_CATALOG_INDEX_INVALID) {
        return prv_reject(J1939_VALUE_STORE_UNKNOWN_KEY);
    }
    if (!prv_is_blob(index) || (data == NULL)) {
        return prv_reject(J1939_VALUE_STORE_UNSUPPORTED_SIGNAL);
    }

    const uint8_t staging = prv_staging_bank();
    if ((s_vs.blob_index[staging] != J1939_CATALOG_INDEX_INVALID) &&
        (s_vs.blob_index[staging] != index)) {
        return prv_reject(J1939_VALUE_STORE_RESOURCE_FULL);
    }
    if ((total_length == 0u) || (total_length > prv_blob_capacity(index)) ||
        (count == 0u) || (offset >= total_length) ||
        (count > (uint16_t)(total_length - offset)) ||
        (s_vs.blob_touched && (total_length != s_vs.blob_length[staging]))) {
        return prv_reject(J1939_VALUE_STORE_VALUE_OUT_OF_RANGE);
    }
    const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[index];
    for (uint16_t i = 0u; i < count; i++) {
        if ((data[i] < sig->raw_min) || (data[i] > sig->raw_max)) {
            return prv_reject(J1939_VALUE_STORE_VALUE_OUT_OF_RANGE);
        }
    }

    /* The first chunk replaces any blob copied from the active bank. */
    if (!s_vs.blob_touched) {
        memset(s_vs.blob[staging], 0, sizeof(s_vs.blob[0]));
        s_vs.blob_length[staging] = total_length;
        s_vs.blob_index[staging] = index;
        s_vs.blob_touched = true;
    }
    for (uint16_t i = 0u; i < count; i++) {
        const uint16_t pos = (uint16_t)(offset + i);
        s_vs.blob[staging][pos] = data[i];
        if (!prv_mask_test(s_vs.blob_written, pos)) {
            prv_mask_set(s_vs.blob_written, pos, true);
            s_vs.blob_written_count++;
        }
    }
    prv_mask_set(s_vs.active_mask[staging], index, true);
    return J1939_VALUE_STORE_OK;
}

uint16_t J1939_Value_Store_Staged_Blob_Bytes(void)
{
    return (s_vs.txn_state == VS_TXN_IDLE) ? 0u : s_vs.blob_written_count;
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
    const uint8_t staging = prv_staging_bank();
    prv_mask_set(s_vs.active_mask[staging], index, false);
    if (s_vs.blob_index[staging] == index) {
        prv_clear_blob(staging);
        memset(s_vs.blob_written, 0, sizeof(s_vs.blob_written));
        s_vs.blob_written_count = 0u;
        s_vs.blob_touched = false;
    }
    return J1939_VALUE_STORE_OK;
}

J1939_Value_Store_Status_t J1939_Value_Store_Commit(uint16_t* out_generation)
{
    J1939_Value_Store_Status_t status = prv_txn_ready();
    const uint8_t staging = prv_staging_bank();

    if ((status == J1939_VALUE_STORE_OK) && s_vs.blob_touched &&
        (s_vs.blob_written_count != s_vs.blob_length[staging])) {
        status = J1939_VALUE_STORE_BLOB_INCOMPLETE;
    }

    /* Whole-bank validation: commit never relies on earlier per-write checks. */
    uint16_t count = 0u;
    for (uint16_t i = 0u; (status == J1939_VALUE_STORE_OK) &&
                          (i < J1939_CATALOG_SIGNAL_COUNT); i++) {
        if (prv_mask_test(s_vs.active_mask[staging], i)) {
            status = prv_is_blob(i)
                ? prv_check_blob(staging, i)
                : prv_check_index(i, s_vs.raw_value[staging][i]);
            count++;
        }
    }
    if ((status == J1939_VALUE_STORE_OK) && ((J1939_CATALOG_SIGNAL_COUNT % 8u) != 0u)) {
        const uint8_t tail = s_vs.active_mask[staging][J1939_CATALOG_ACTIVE_MASK_BYTES - 1u];
        if ((tail >> (J1939_CATALOG_SIGNAL_COUNT % 8u)) != 0u) {
            status = J1939_VALUE_STORE_UNKNOWN_KEY;
        }
    }
    if ((status == J1939_VALUE_STORE_OK) &&
        (s_vs.blob_index[staging] != J1939_CATALOG_INDEX_INVALID) &&
        !prv_mask_test(s_vs.active_mask[staging], s_vs.blob_index[staging])) {
        prv_clear_blob(staging);    /* blob slot owned by a removed record */
    }

    if (status == J1939_VALUE_STORE_OK) {
        s_vs.active_count[staging] = count;
        s_vs.config_crc[staging] = prv_config_crc(staging);
        s_vs.active_bank = staging;
        s_vs.generation = (uint16_t)(s_vs.generation + 1u);
        if (s_vs.generation == 0u) {
            s_vs.generation = 1u;
        }
    }
    /* A failed commit discards the staging bank. */
    s_vs.txn_state = VS_TXN_IDLE;
    s_vs.blob_touched = false;
    s_vs.blob_written_count = 0u;
    if (out_generation != NULL) {
        *out_generation = s_vs.generation;
    }
    return status;
}

void J1939_Value_Store_Abort(void)
{
    s_vs.txn_state = VS_TXN_IDLE;
    s_vs.blob_touched = false;
    s_vs.blob_written_count = 0u;
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
    if ((out_raw == NULL) || !J1939_Value_Store_Is_Active(catalog_index) ||
        prv_is_blob(catalog_index)) {
        return false;
    }
    *out_raw = s_vs.raw_value[s_vs.active_bank][catalog_index];
    return true;
}

bool J1939_Value_Store_Get_Blob(uint16_t catalog_index,
                                const uint8_t** out_data,
                                uint16_t* out_length)
{
    const uint8_t bank = s_vs.active_bank;
    if ((out_data == NULL) || (out_length == NULL) ||
        !J1939_Value_Store_Is_Active(catalog_index) ||
        (s_vs.blob_index[bank] != catalog_index)) {
        return false;
    }
    *out_data = s_vs.blob[bank];
    *out_length = s_vs.blob_length[bank];
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

uint32_t J1939_Value_Store_Get_Config_Crc(void)
{
    return s_vs.config_crc[s_vs.active_bank];
}
