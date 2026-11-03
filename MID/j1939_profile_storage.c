#include "j1939_profile_storage.h"
#include "hal_flash.h"
#include "j1939_crc32.h"
#include "j1939_customer_profiles.h"
#include "j1939_signal_definitions.h"
#include "j1939_value_store.h"
#include <stddef.h>
#include <string.h>

#define STORAGE_MAGIC                 0x3650394Au /* "J9P6" little-endian */
#define STORAGE_VALID_MARKER          0xA55AA55Au
#define STORAGE_HEADER_BYTES          44u
#define STORAGE_IMAGE_MAX_BYTES       1536u
#define STORAGE_SLOT_COUNT            2u
#define STORAGE_PAGES_PER_SLOT        4u

#define OFF_MAGIC                      0u
#define OFF_FORMAT_VERSION             4u
#define OFF_HEADER_LENGTH              6u
#define OFF_CATALOG_CRC                8u
#define OFF_SEQUENCE                  12u
#define OFF_CONFIG_CRC                16u
#define OFF_IMAGE_CRC                 20u
#define OFF_BITRATE_BPS               24u
#define OFF_PAYLOAD_LENGTH            28u
#define OFF_SIGNAL_COUNT              30u
#define OFF_SCALAR_COUNT              32u 
#define OFF_PROFILE_ID                34u
#define OFF_BLOB_COUNT                35u
#define OFF_FLAGS                     36u
#define OFF_RESERVED                  38u
#define OFF_VALID_MARKER              40u

static uint8_t s_image[STORAGE_IMAGE_MAX_BYTES];

static uint16_t prv_read_u16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t prv_read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void prv_write_u16(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void prv_write_u32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t prv_slot_address(uint8_t slot)
{
    return (slot == 0u) ? HAL_FLASH_PROFILE_SLOT_A_ADDRESS
                        : HAL_FLASH_PROFILE_SLOT_B_ADDRESS;
}

static bool prv_sequence_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

static uint32_t prv_image_crc(uint16_t total_length)
{
    const uint32_t stored_crc = prv_read_u32(&s_image[OFF_IMAGE_CRC]);
    const uint32_t stored_marker = prv_read_u32(&s_image[OFF_VALID_MARKER]);
    prv_write_u32(&s_image[OFF_IMAGE_CRC], 0u);
    prv_write_u32(&s_image[OFF_VALID_MARKER], 0xFFFFFFFFu);
    uint32_t crc = J1939_Crc32_Update(J1939_CRC32_INIT, s_image,
                                      total_length);
    crc = J1939_Crc32_Final(crc);
    prv_write_u32(&s_image[OFF_IMAGE_CRC], stored_crc);
    prv_write_u32(&s_image[OFF_VALID_MARKER], stored_marker);
    return crc;
}

static uint16_t prv_blob_capacity(uint16_t catalog_index)
{
    uint16_t bytes = (uint16_t)(
        J1939_Catalog_Signals[catalog_index].bit_length / 8u);
    return (bytes < J1939_VALUE_STORE_BLOB_MAX_BYTES)
        ? bytes : J1939_VALUE_STORE_BLOB_MAX_BYTES;
}

static Profile_Storage_Status_t prv_parse(
    Profile_Storage_Info_t* info,
    Profile_Storage_Scalar_Visitor_t scalar_visitor,
    Profile_Storage_Blob_Visitor_t blob_visitor,
    void* context)
{
    const uint16_t payload_length = prv_read_u16(&s_image[OFF_PAYLOAD_LENGTH]);
    const uint16_t signal_count = prv_read_u16(&s_image[OFF_SIGNAL_COUNT]);
    const uint16_t scalar_count = prv_read_u16(&s_image[OFF_SCALAR_COUNT]);
    const uint8_t blob_count = s_image[OFF_BLOB_COUNT];
    if ((signal_count > J1939_CATALOG_SIGNAL_COUNT) ||
        ((uint16_t)(scalar_count + blob_count) != signal_count) ||
        (blob_count > 1u)) {
        return PROFILE_STORAGE_INVALID_IMAGE;
    }

    uint8_t seen[J1939_CATALOG_ACTIVE_MASK_BYTES];
    memset(seen, 0, sizeof(seen));
    uint16_t offset = STORAGE_HEADER_BYTES;
    const uint16_t end = (uint16_t)(STORAGE_HEADER_BYTES + payload_length);
    uint16_t actual_scalars = 0u;
    uint8_t actual_blobs = 0u;
    uint16_t previous_index = J1939_CATALOG_INDEX_INVALID;
    static const uint8_t config_magic[8] = {
        'J', '1', '9', '3', '9', 'C', 'F', 'G'
    };
    uint32_t config_crc = J1939_Crc32_Update(J1939_CRC32_INIT,
                                             config_magic,
                                             sizeof(config_magic));
    config_crc = J1939_Crc32_Update_U32_LE(config_crc,
                                           J1939_CATALOG_CRC32);

    for (uint16_t record = 0u; record < signal_count; record++) {
        if ((uint16_t)(end - offset) < 2u) {
            return PROFILE_STORAGE_INVALID_IMAGE;
        }
        const uint16_t key = prv_read_u16(&s_image[offset]);
        offset = (uint16_t)(offset + 2u);
        const uint16_t index = J1939_Catalog_Index_From_Key(key);
        if ((index == J1939_CATALOG_INDEX_INVALID) ||
            ((previous_index != J1939_CATALOG_INDEX_INVALID) &&
             (index <= previous_index))) {
            return PROFILE_STORAGE_INVALID_IMAGE;
        }
        previous_index = index;
        const uint8_t bit = (uint8_t)(1u << (index % 8u));
        if ((seen[index / 8u] & bit) != 0u) {
            return PROFILE_STORAGE_INVALID_IMAGE;
        }
        seen[index / 8u] |= bit;
        config_crc = J1939_Crc32_Update_U16_LE(config_crc, key);

        const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[index];
        if ((signal->flags & J1939_CAT_SIG_BLOB) != 0u) {
            if ((uint16_t)(end - offset) < 2u) {
                return PROFILE_STORAGE_INVALID_IMAGE;
            }
            const uint16_t length = prv_read_u16(&s_image[offset]);
            offset = (uint16_t)(offset + 2u);
            if ((length == 0u) || (length > prv_blob_capacity(index)) ||
                ((uint16_t)(end - offset) < length)) {
                return PROFILE_STORAGE_INVALID_IMAGE;
            }
            for (uint16_t i = 0u; i < length; i++) {
                if ((s_image[offset + i] < signal->raw_min) ||
                    (s_image[offset + i] > signal->raw_max)) {
                    return PROFILE_STORAGE_INVALID_IMAGE;
                }
            }
            config_crc = J1939_Crc32_Update_U16_LE(config_crc, length);
            config_crc = J1939_Crc32_Update(config_crc, &s_image[offset],
                                             length);
            if ((blob_visitor != NULL) &&
                !blob_visitor(key, &s_image[offset], length, context)) {
                return PROFILE_STORAGE_VISITOR_REJECTED;
            }
            offset = (uint16_t)(offset + length);
            actual_blobs++;
        } else {
            if ((uint16_t)(end - offset) < 4u) {
                return PROFILE_STORAGE_INVALID_IMAGE;
            }
            const uint32_t raw = prv_read_u32(&s_image[offset]);
            offset = (uint16_t)(offset + 4u);
            if ((raw < signal->raw_min) || (raw > signal->raw_max)) {
                return PROFILE_STORAGE_INVALID_IMAGE;
            }
            config_crc = J1939_Crc32_Update_U32_LE(config_crc, raw);
            if ((scalar_visitor != NULL) &&
                !scalar_visitor(key, raw, context)) {
                return PROFILE_STORAGE_VISITOR_REJECTED;
            }
            actual_scalars++;
        }
    }
    config_crc = J1939_Crc32_Final(config_crc);
    if ((offset != end) || (actual_scalars != scalar_count) ||
        (actual_blobs != blob_count) ||
        (config_crc != prv_read_u32(&s_image[OFF_CONFIG_CRC]))) {
        return PROFILE_STORAGE_INVALID_IMAGE;
    }

    const uint8_t profile_id = s_image[OFF_PROFILE_ID];
    if (profile_id != J1939_PROFILE_ID_CUSTOM) {
        const J1939_Customer_Profile_t* profile =
            J1939_Customer_Profile_Find(profile_id);
        if ((profile == NULL) || (profile->signal_count != signal_count)) {
            return PROFILE_STORAGE_INVALID_IMAGE;
        }
        for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
            const bool present =
                (seen[i / 8u] & (uint8_t)(1u << (i % 8u))) != 0u;
            const bool required = J1939_Customer_Profile_Has_Key(
                profile, J1939_Catalog_Signals[i].signal_key);
            if (present != required) {
                return PROFILE_STORAGE_INVALID_IMAGE;
            }
        }
    }

    if (info != NULL) {
        info->sequence = prv_read_u32(&s_image[OFF_SEQUENCE]);
        info->config_crc = prv_read_u32(&s_image[OFF_CONFIG_CRC]);
        info->bitrate_bps = prv_read_u32(&s_image[OFF_BITRATE_BPS]);
        info->signal_count = signal_count;
        info->scalar_count = scalar_count;
        info->flags = prv_read_u16(&s_image[OFF_FLAGS]);
        info->profile_id = profile_id;
        info->blob_count = blob_count;
    }
    return PROFILE_STORAGE_OK;
}

static Profile_Storage_Status_t prv_load_slot(uint8_t slot,
                                               Profile_Storage_Info_t* info)
{
    const uint32_t address = prv_slot_address(slot);
    if (hal_flash_read(address, s_image, STORAGE_HEADER_BYTES) != HAL_FLASH_OK) {
        return PROFILE_STORAGE_FLASH_ERROR;
    }
    if ((prv_read_u32(&s_image[OFF_MAGIC]) != STORAGE_MAGIC) ||
        (prv_read_u16(&s_image[OFF_FORMAT_VERSION]) !=
         J1939_PROFILE_STORAGE_FORMAT_VERSION) ||
        (prv_read_u16(&s_image[OFF_HEADER_LENGTH]) != STORAGE_HEADER_BYTES) ||
        (prv_read_u32(&s_image[OFF_VALID_MARKER]) != STORAGE_VALID_MARKER) ||
        (prv_read_u16(&s_image[OFF_RESERVED]) != 0u)) {
        return PROFILE_STORAGE_INVALID_IMAGE;
    }
    if (prv_read_u32(&s_image[OFF_CATALOG_CRC]) != J1939_CATALOG_CRC32) {
        return PROFILE_STORAGE_CATALOG_MISMATCH;
    }
    const uint16_t payload_length = prv_read_u16(&s_image[OFF_PAYLOAD_LENGTH]);
    const uint16_t total_length =
        (uint16_t)(STORAGE_HEADER_BYTES + payload_length);
    if ((total_length > STORAGE_IMAGE_MAX_BYTES) ||
        (total_length > HAL_FLASH_PROFILE_SLOT_SIZE)) {
        return PROFILE_STORAGE_INVALID_IMAGE;
    }
    if (hal_flash_read(address, s_image, total_length) != HAL_FLASH_OK) {
        return PROFILE_STORAGE_FLASH_ERROR;
    }
    if (prv_image_crc(total_length) != prv_read_u32(&s_image[OFF_IMAGE_CRC])) {
        return PROFILE_STORAGE_INVALID_IMAGE;
    }
    Profile_Storage_Status_t status = prv_parse(info, NULL, NULL, NULL);
    if ((status == PROFILE_STORAGE_OK) && (info != NULL)) {
        info->slot = slot;
    }
    return status;
}

void Profile_Storage_Init(void)
{
    memset(s_image, 0, sizeof(s_image));
}

Profile_Storage_Status_t Profile_Storage_Open_Latest(
    Profile_Storage_Info_t* out)
{
    Profile_Storage_Info_t a;
    Profile_Storage_Info_t b;
    const Profile_Storage_Status_t sa = prv_load_slot(0u, &a);
    const Profile_Storage_Status_t sb = prv_load_slot(1u, &b);
    uint8_t selected;
    if ((sa == PROFILE_STORAGE_OK) && (sb == PROFILE_STORAGE_OK)) {
        selected = prv_sequence_newer(b.sequence, a.sequence) ? 1u : 0u;
    } else if (sa == PROFILE_STORAGE_OK) {
        selected = 0u;
    } else if (sb == PROFILE_STORAGE_OK) {
        selected = 1u;
    } else if ((sa == PROFILE_STORAGE_CATALOG_MISMATCH) ||
               (sb == PROFILE_STORAGE_CATALOG_MISMATCH)) {
        return PROFILE_STORAGE_CATALOG_MISMATCH;
    } else if ((sa == PROFILE_STORAGE_FLASH_ERROR) ||
               (sb == PROFILE_STORAGE_FLASH_ERROR)) {
        return PROFILE_STORAGE_FLASH_ERROR;
    } else {
        return PROFILE_STORAGE_EMPTY;
    }
    return prv_load_slot(selected, out);
}

static Profile_Storage_Status_t prv_build_image(uint8_t profile_id,
                                                 uint32_t bitrate_bps,
                                                 bool autostart,
                                                 uint32_t sequence,
                                                 uint16_t* out_total)
{
    memset(s_image, 0xFF, sizeof(s_image));
    uint16_t offset = STORAGE_HEADER_BYTES;
    uint16_t scalar_count = 0u;
    uint8_t blob_count = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        if (!J1939_Value_Store_Is_Active(i)) {
            continue;
        }
        const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[i];
        const uint16_t key = signal->signal_key;
        if ((signal->flags & J1939_CAT_SIG_BLOB) != 0u) {
            const uint8_t* data = NULL;
            uint16_t length = 0u;
            if (!J1939_Value_Store_Get_Blob(i, &data, &length) ||
                ((uint32_t)offset + 4u + length > sizeof(s_image))) {
                return PROFILE_STORAGE_UNSUPPORTED_CONFIG;
            }
            prv_write_u16(&s_image[offset], key);
            prv_write_u16(&s_image[offset + 2u], length);
            memcpy(&s_image[offset + 4u], data, length);
            offset = (uint16_t)(offset + 4u + length);
            blob_count++;
        } else {
            uint32_t raw = 0u;
            if (!J1939_Value_Store_Get_Raw(i, &raw) ||
                ((uint32_t)offset + 6u > sizeof(s_image))) {
                return PROFILE_STORAGE_UNSUPPORTED_CONFIG;
            }
            prv_write_u16(&s_image[offset], key);
            prv_write_u32(&s_image[offset + 2u], raw);
            offset = (uint16_t)(offset + 6u);
            scalar_count++;
        }
    }
    const uint16_t signal_count = (uint16_t)(scalar_count + blob_count);
    if ((signal_count == 0u) || (blob_count > 1u)) {
        return PROFILE_STORAGE_UNSUPPORTED_CONFIG;
    }

    prv_write_u32(&s_image[OFF_MAGIC], STORAGE_MAGIC);
    prv_write_u16(&s_image[OFF_FORMAT_VERSION],
                  J1939_PROFILE_STORAGE_FORMAT_VERSION);
    prv_write_u16(&s_image[OFF_HEADER_LENGTH], STORAGE_HEADER_BYTES);
    prv_write_u32(&s_image[OFF_CATALOG_CRC], J1939_CATALOG_CRC32);
    prv_write_u32(&s_image[OFF_SEQUENCE], sequence);
    prv_write_u32(&s_image[OFF_CONFIG_CRC],
                  J1939_Value_Store_Get_Config_Crc());
    prv_write_u32(&s_image[OFF_IMAGE_CRC], 0u);
    prv_write_u32(&s_image[OFF_BITRATE_BPS], bitrate_bps);
    prv_write_u16(&s_image[OFF_PAYLOAD_LENGTH],
                  (uint16_t)(offset - STORAGE_HEADER_BYTES));
    prv_write_u16(&s_image[OFF_SIGNAL_COUNT], signal_count);
    prv_write_u16(&s_image[OFF_SCALAR_COUNT], scalar_count);
    s_image[OFF_PROFILE_ID] = profile_id;
    s_image[OFF_BLOB_COUNT] = blob_count;
    prv_write_u16(&s_image[OFF_FLAGS], autostart
        ? J1939_PROFILE_STORAGE_FLAG_AUTOSTART : 0u);
    prv_write_u16(&s_image[OFF_RESERVED], 0u);
    prv_write_u32(&s_image[OFF_VALID_MARKER], 0xFFFFFFFFu);
    prv_write_u32(&s_image[OFF_IMAGE_CRC], prv_image_crc(offset));
    *out_total = offset;
    return PROFILE_STORAGE_OK;
}

static Profile_Storage_Status_t prv_write_slot(uint8_t slot,
                                                uint16_t total_length,
                                                Profile_Storage_Info_t* out)
{
    const uint32_t address = prv_slot_address(slot);
    for (uint8_t page = 0u; page < STORAGE_PAGES_PER_SLOT; page++) {
        if (hal_flash_erase_page(address +
            (uint32_t)page * HAL_FLASH_PAGE_SIZE) != HAL_FLASH_OK) {
            return PROFILE_STORAGE_FLASH_ERROR;
        }
    }
    const uint16_t programmed = (uint16_t)((total_length + 1u) & ~1u);
    for (uint16_t offset = 0u; offset < programmed; offset += 2u) {
        if ((offset == OFF_VALID_MARKER) ||
            (offset == (OFF_VALID_MARKER + 2u))) {
            continue;
        }
        const uint16_t value = prv_read_u16(&s_image[offset]);
        if (hal_flash_program_halfword(address + offset, value) !=
            HAL_FLASH_OK) {
            return PROFILE_STORAGE_FLASH_ERROR;
        }
    }
    for (uint16_t offset = 0u; offset < programmed; offset += 2u) {
        if ((offset == OFF_VALID_MARKER) ||
            (offset == (OFF_VALID_MARKER + 2u))) {
            continue;
        }
        uint8_t actual[2];
        if ((hal_flash_read(address + offset, actual, sizeof(actual)) !=
             HAL_FLASH_OK) ||
            (actual[0] != s_image[offset]) ||
            (actual[1] != s_image[offset + 1u])) {
            return PROFILE_STORAGE_VERIFY_ERROR;
        }
    }

    if ((hal_flash_program_halfword(address + OFF_VALID_MARKER,
                                    (uint16_t)STORAGE_VALID_MARKER) !=
         HAL_FLASH_OK) ||
        (hal_flash_program_halfword(address + OFF_VALID_MARKER + 2u,
            (uint16_t)(STORAGE_VALID_MARKER >> 16)) != HAL_FLASH_OK)) {
        return PROFILE_STORAGE_FLASH_ERROR;
    }
    const Profile_Storage_Status_t status = prv_load_slot(slot, out);
    return (status == PROFILE_STORAGE_OK) ? PROFILE_STORAGE_OK
                                          : PROFILE_STORAGE_VERIFY_ERROR;
}

Profile_Storage_Status_t Profile_Storage_Save(uint8_t profile_id,
                                               uint32_t bitrate_bps,
                                               bool autostart,
                                               Profile_Storage_Info_t* out)
{
    Profile_Storage_Info_t current;
    const Profile_Storage_Status_t open = Profile_Storage_Open_Latest(&current);
    uint32_t sequence = 1u;
    uint8_t target = 0u;
    if (open == PROFILE_STORAGE_OK) {
        sequence = current.sequence + 1u;
        target = (uint8_t)(current.slot ^ 1u);
    } else if ((open != PROFILE_STORAGE_EMPTY) &&
               (open != PROFILE_STORAGE_CATALOG_MISMATCH)) {
        return open;
    }
    uint16_t total_length = 0u;
    Profile_Storage_Status_t status = prv_build_image(
        profile_id, bitrate_bps, autostart, sequence, &total_length);
    return (status == PROFILE_STORAGE_OK)
        ? prv_write_slot(target, total_length, out) : status;
}

Profile_Storage_Status_t Profile_Storage_Visit_Latest(
    Profile_Storage_Scalar_Visitor_t scalar_visitor,
    Profile_Storage_Blob_Visitor_t blob_visitor,
    void* context,
    Profile_Storage_Info_t* out)
{
    Profile_Storage_Info_t info;
    Profile_Storage_Status_t status = Profile_Storage_Open_Latest(&info);
    if (status != PROFILE_STORAGE_OK) {
        return status;
    }
    status = prv_parse(&info, scalar_visitor, blob_visitor, context);
    if ((status == PROFILE_STORAGE_OK) && (out != NULL)) {
        *out = info;
    }
    return status;
}

Profile_Storage_Status_t Profile_Storage_Set_Autostart(
    bool enabled, Profile_Storage_Info_t* out)
{
    Profile_Storage_Info_t current;
    Profile_Storage_Status_t status = Profile_Storage_Open_Latest(&current);
    if (status != PROFILE_STORAGE_OK) {
        return status;
    }
    const uint8_t target = (uint8_t)(current.slot ^ 1u);
    prv_write_u32(&s_image[OFF_SEQUENCE], current.sequence + 1u);
    prv_write_u16(&s_image[OFF_FLAGS], enabled
        ? J1939_PROFILE_STORAGE_FLAG_AUTOSTART : 0u);
    prv_write_u32(&s_image[OFF_VALID_MARKER], 0xFFFFFFFFu);
    const uint16_t total_length = (uint16_t)(
        STORAGE_HEADER_BYTES + prv_read_u16(&s_image[OFF_PAYLOAD_LENGTH]));
    prv_write_u32(&s_image[OFF_IMAGE_CRC], 0u);
    prv_write_u32(&s_image[OFF_IMAGE_CRC], prv_image_crc(total_length));
    return prv_write_slot(target, total_length, out);
}

Profile_Storage_Status_t Profile_Storage_Erase(void)
{
    for (uint8_t slot = 0u; slot < STORAGE_SLOT_COUNT; slot++) {
        const uint32_t address = prv_slot_address(slot);
        for (uint8_t page = 0u; page < STORAGE_PAGES_PER_SLOT; page++) {
            if (hal_flash_erase_page(address +
                (uint32_t)page * HAL_FLASH_PAGE_SIZE) != HAL_FLASH_OK) {
                return PROFILE_STORAGE_FLASH_ERROR;
            }
        }
    }
    return PROFILE_STORAGE_OK;
}

_Static_assert(STORAGE_IMAGE_MAX_BYTES < HAL_FLASH_PROFILE_SLOT_SIZE,
               "persistence image buffer must fit one slot");
