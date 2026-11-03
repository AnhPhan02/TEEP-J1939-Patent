/*
 * =============================================================================
 * FILE : j1939_crc32.h
 * WHAT : CRC-32/ISO-HDLC (zlib/PNG polynomial 0xEDB88320, reflected, init and
 *        final XOR 0xFFFFFFFF). Same algorithm as the catalog CRC, so host
 *        tools can compute it with zlib.crc32().
 * =============================================================================
 */

#ifndef J1939_CRC32_H
#define J1939_CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define J1939_CRC32_INIT  0xFFFFFFFFu

/* Streaming use: crc = J1939_CRC32_INIT; crc = Update(crc, ...)...;
 * result = J1939_Crc32_Final(crc). */
uint32_t J1939_Crc32_Update(uint32_t crc, const uint8_t* data, uint16_t length);
uint32_t J1939_Crc32_Update_U8(uint32_t crc, uint8_t value);
uint32_t J1939_Crc32_Update_U16_LE(uint32_t crc, uint16_t value);
uint32_t J1939_Crc32_Update_U32_LE(uint32_t crc, uint32_t value);

static inline uint32_t J1939_Crc32_Final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

#ifdef __cplusplus
}
#endif
#endif /* J1939_CRC32_H */
