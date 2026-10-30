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
 *  Blob records (VIN) are double-banked the same way in one blob slot.
 *
 *  Configuration CRC (CRC-32/ISO-HDLC, see j1939_crc32.h) identifies the
 *  active configuration independently of generation, so equal configurations
 *  have equal CRCs. It is computed over, in this order:
 *      "J1939CFG" (8 ASCII bytes), catalog CRC32 (u32 LE),
 *      then for every active record in ascending catalog index:
 *          signal_key (u16 LE) followed by
 *          scalar: raw value (u32 LE)
 *          blob:   length (u16 LE) and the blob bytes.
 *  Waveform (pattern pool) configuration is not part of it.
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
    J1939_VALUE_STORE_TRANSACTION_REJECTED, /* an earlier write in it failed */
    J1939_VALUE_STORE_BLOB_INCOMPLETE,      /* commit: staged blob has unwritten bytes */
    J1939_VALUE_STORE_RESOURCE_FULL         /* a second blob record in one bank */
} J1939_Value_Store_Status_t;

/* One blob slot per bank, sized for the largest catalog blob (VIN 200 B). */
#define J1939_VALUE_STORE_BLOB_MAX_BYTES  J1939_CATALOG_MAX_PAYLOAD_BYTES

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
/* Stage count bytes of the blob record signal_key at offset. total_length is
 * the blob's transmitted length (1..catalog bytes). The first chunk of a
 * transaction fixes it and clears the staged bytes; later chunks must repeat
 * it. Every byte must lie inside the catalog raw bounds. Commit fails with
 * BLOB_INCOMPLETE until bytes 0..total_length-1 were all written. */
J1939_Value_Store_Status_t J1939_Value_Store_Write_Blob(uint16_t signal_key,
                                                        uint16_t total_length,
                                                        uint16_t offset,
                                                        const uint8_t* data,
                                                        uint16_t count);
/* Number of distinct blob bytes written in the open transaction. */
uint16_t J1939_Value_Store_Staged_Blob_Bytes(void);
/* out_generation (may be NULL) receives the active generation afterwards. */
J1939_Value_Store_Status_t J1939_Value_Store_Commit(uint16_t* out_generation);
void J1939_Value_Store_Abort(void);
bool J1939_Value_Store_In_Transaction(void);

/* Read-only validation of one value against the catalog. */
J1939_Value_Store_Status_t J1939_Value_Store_Check_Raw(uint16_t signal_key,
                                                       uint32_t raw_value);

/* ---- active bank (catalog index, see J1939_Catalog_Index_From_Key) ------- */
bool     J1939_Value_Store_Is_Active(uint16_t catalog_index);
/* Scalar records only; false for an inactive or blob record. */
bool     J1939_Value_Store_Get_Raw(uint16_t catalog_index, uint32_t* out_raw);
/* Active blob bytes (valid until the next commit). */
bool     J1939_Value_Store_Get_Blob(uint16_t catalog_index,
                                    const uint8_t** out_data,
                                    uint16_t* out_length);
uint32_t J1939_Value_Store_Get_Config_Crc(void);
uint16_t J1939_Value_Store_Get_Active_Count(void);
/* Incremented by every successful commit; 0 means never committed. */
uint16_t J1939_Value_Store_Get_Generation(void);

#ifdef __cplusplus
}
#endif
#endif /* J1939_VALUE_STORE_H */
