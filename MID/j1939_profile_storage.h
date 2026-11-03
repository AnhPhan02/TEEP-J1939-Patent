/* Power-fail-safe A/B persistence for the active constant/blob profile. */
#ifndef J1939_PROFILE_STORAGE_H
#define J1939_PROFILE_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define J1939_PROFILE_STORAGE_FORMAT_VERSION  1u
#define J1939_PROFILE_STORAGE_FLAG_AUTOSTART  0x0001u

typedef enum {
    PROFILE_STORAGE_OK = 0,
    PROFILE_STORAGE_EMPTY,
    PROFILE_STORAGE_BUSY,
    PROFILE_STORAGE_INVALID_IMAGE,
    PROFILE_STORAGE_CATALOG_MISMATCH,
    PROFILE_STORAGE_FLASH_ERROR,
    PROFILE_STORAGE_VERIFY_ERROR,
    PROFILE_STORAGE_UNSUPPORTED_CONFIG,
    PROFILE_STORAGE_VISITOR_REJECTED
} Profile_Storage_Status_t;

typedef struct {
    uint32_t sequence;
    uint32_t config_crc;
    uint32_t bitrate_bps;
    uint16_t signal_count;
    uint16_t scalar_count;
    uint16_t flags;
    uint8_t profile_id;
    uint8_t blob_count;
    uint8_t slot;              /* 0=A, 1=B */
} Profile_Storage_Info_t;

typedef bool (*Profile_Storage_Scalar_Visitor_t)(uint16_t signal_key,
                                                  uint32_t raw_value,
                                                  void* context);
typedef bool (*Profile_Storage_Blob_Visitor_t)(uint16_t signal_key,
                                                const uint8_t* data,
                                                uint16_t length,
                                                void* context);

void Profile_Storage_Init(void);
Profile_Storage_Status_t Profile_Storage_Save(uint8_t profile_id,
                                               uint32_t bitrate_bps,
                                               bool autostart,
                                               Profile_Storage_Info_t* out);
Profile_Storage_Status_t Profile_Storage_Open_Latest(
    Profile_Storage_Info_t* out);
Profile_Storage_Status_t Profile_Storage_Visit_Latest(
    Profile_Storage_Scalar_Visitor_t scalar_visitor,
    Profile_Storage_Blob_Visitor_t blob_visitor,
    void* context,
    Profile_Storage_Info_t* out);
Profile_Storage_Status_t Profile_Storage_Set_Autostart(
    bool enabled, Profile_Storage_Info_t* out);
Profile_Storage_Status_t Profile_Storage_Erase(void);

#ifdef __cplusplus
}
#endif
#endif /* J1939_PROFILE_STORAGE_H */
