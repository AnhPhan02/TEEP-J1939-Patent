/*
 * =============================================================================
 * FILE : j1939_value_store.h
 * WHAT : Compact raw-value store for all catalog records: double-banked raw
 *        values and active bitmaps with an atomic staging -> active commit.
 *
 *  Writers open a transaction, stage values, then commit. Commit re-validates
 *  the complete staging bank and swaps banks in one step; readers (scheduler)
 *  only ever see the active bank, so a PGN never mixes two configurations.
 *  Any rejected write poisons the transaction: commit then fails and the
 *  active bank and generation stay unchanged.
 *
 *  Single-threaded: call from the main loop only (no ISR access).
 * =============================================================================
 */

#ifndef J1939_VALUE_STORE_H
#define J1939_VALUE_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_signal_definitions.h"

typedef enum {
    J1939_VALUE_STORE_OK = 0,
    J1939_VALUE_STORE_UNKNOWN_KEY,
    J1939_VALUE_STORE_UNSUPPORTED_SIGNAL,   /* blob/non-scalar record */
    J1939_VALUE_STORE_VALUE_OUT_OF_RANGE,   /* outside catalog raw_min..raw_max */
    J1939_VALUE_STORE_NO_TRANSACTION,
    J1939_VALUE_STORE_TRANSACTION_OPEN,
    J1939_VALUE_STORE_TRANSACTION_REJECTED  /* an earlier write in it failed */
} J1939_Value_Store_Status_t;

typedef enum {
    J1939_VALUE_STORE_BEGIN_EMPTY = 0,      /* staging starts with nothing active */
    J1939_VALUE_STORE_BEGIN_COPY_ACTIVE     /* staging starts as the active bank */
} J1939_Value_Store_Begin_t;

/* Both banks empty, generation 0, no transaction. */
void J1939_Value_Store_Init(void);

/* ---- transaction (staging bank) ----------------------------------------- */
J1939_Value_Store_Status_t J1939_Value_Store_Begin(J1939_Value_Store_Begin_t start);
J1939_Value_Store_Status_t J1939_Value_Store_Set_Raw(uint16_t signal_key,
                                                     uint32_t raw_value);
J1939_Value_Store_Status_t J1939_Value_Store_Remove(uint16_t signal_key);
/* out_generation (may be NULL) receives the active generation afterwards. */
J1939_Value_Store_Status_t J1939_Value_Store_Commit(uint16_t* out_generation);
void J1939_Value_Store_Abort(void);
bool J1939_Value_Store_In_Transaction(void);

/* Read-only validation of one value against the catalog. */
J1939_Value_Store_Status_t J1939_Value_Store_Check_Raw(uint16_t signal_key,
                                                       uint32_t raw_value);

/* ---- active bank (catalog index, see J1939_Catalog_Index_From_Key) ------- */
bool     J1939_Value_Store_Is_Active(uint16_t catalog_index);
bool     J1939_Value_Store_Get_Raw(uint16_t catalog_index, uint32_t* out_raw);
uint16_t J1939_Value_Store_Get_Active_Count(void);
/* Incremented by every successful commit; 0 means never committed. */
uint16_t J1939_Value_Store_Get_Generation(void);

#ifdef __cplusplus
}
#endif
#endif /* J1939_VALUE_STORE_H */
