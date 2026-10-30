/*
 * =============================================================================
 * FILE : j1939_crc32.c
 * WHAT : Table-less CRC-32/ISO-HDLC (small Flash footprint, no RAM table).
 * =============================================================================
 */

#include "j1939_crc32.h"
#include <stddef.h>

uint32_t J1939_Crc32_Update_U8(uint32_t crc, uint8_t value)
{
    crc ^= value;
    for (uint8_t bit = 0u; bit < 8u; bit++) {
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

uint32_t J1939_Crc32_Update(uint32_t crc, const uint8_t* data, uint16_t length)
{
    if (data == NULL) {
        return crc;
    }
    for (uint16_t i = 0u; i < length; i++) {
        crc = J1939_Crc32_Update_U8(crc, data[i]);
    }
    return crc;
}

uint32_t J1939_Crc32_Update_U16_LE(uint32_t crc, uint16_t value)
{
    crc = J1939_Crc32_Update_U8(crc, (uint8_t)value);
    return J1939_Crc32_Update_U8(crc, (uint8_t)(value >> 8));
}

uint32_t J1939_Crc32_Update_U32_LE(uint32_t crc, uint32_t value)
{
    crc = J1939_Crc32_Update_U16_LE(crc, (uint16_t)value);
    return J1939_Crc32_Update_U16_LE(crc, (uint16_t)(value >> 16));
}
