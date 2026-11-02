#include "app_persistence.h"
#include "app_generator.h"
#include "j1939_value_store.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    bool ok;
} App_Load_Context_t;

static App_Persistence_Status_t s_last_status = APP_PERSISTENCE_EMPTY;

static App_Persistence_Status_t prv_map(Profile_Storage_Status_t status)
{
    switch (status) {
        case PROFILE_STORAGE_OK: return APP_PERSISTENCE_OK;
        case PROFILE_STORAGE_EMPTY: return APP_PERSISTENCE_EMPTY;
        case PROFILE_STORAGE_CATALOG_MISMATCH:
            return APP_PERSISTENCE_CATALOG_MISMATCH;
        case PROFILE_STORAGE_INVALID_IMAGE:
            return APP_PERSISTENCE_INVALID_IMAGE;
        case PROFILE_STORAGE_VERIFY_ERROR:
            return APP_PERSISTENCE_VERIFY_ERROR;
        case PROFILE_STORAGE_UNSUPPORTED_CONFIG:
            return APP_PERSISTENCE_UNSUPPORTED_CONFIG;
        case PROFILE_STORAGE_VISITOR_REJECTED:
            return APP_PERSISTENCE_APPLY_ERROR;
        case PROFILE_STORAGE_BUSY:
            return APP_PERSISTENCE_INVALID_STATE;
        case PROFILE_STORAGE_FLASH_ERROR:
        default:
            return APP_PERSISTENCE_FLASH_ERROR;
    }
}

static bool prv_can_mutate(void)
{
    return !App_Gen_Is_Running() && !App_Gen_Config_Transaction_Open();
}

static bool prv_load_scalar(uint16_t key, uint32_t raw, void* context)
{
    App_Load_Context_t* load = (App_Load_Context_t*)context;
    if (App_Gen_Stage_Constant_Raw(key, raw, NULL) != APP_CONFIG_TXN_OK) {
        load->ok = false;
    }
    return load->ok;
}

static bool prv_load_blob(uint16_t key, const uint8_t* data, uint16_t length,
                          void* context)
{
    App_Load_Context_t* load = (App_Load_Context_t*)context;
    for (uint16_t offset = 0u; (offset < length) && load->ok;
         offset = (uint16_t)(offset + APP_BLOB_CHUNK_BYTES)) {
        uint8_t chunk[APP_BLOB_CHUNK_BYTES] = { 0u, 0u, 0u };
        uint16_t remaining = (uint16_t)(length - offset);
        uint16_t count = (remaining < APP_BLOB_CHUNK_BYTES)
            ? remaining : APP_BLOB_CHUNK_BYTES;
        memcpy(chunk, &data[offset], count);
        if (App_Gen_Stage_Blob_Chunk(key, (uint8_t)length,
                                    (uint8_t)offset, chunk, NULL) !=
            APP_CONFIG_TXN_OK) {
            load->ok = false;
        }
    }
    return load->ok;
}

void App_Persistence_Init(void)
{
    Profile_Storage_Init();
    s_last_status = APP_PERSISTENCE_EMPTY;
}

App_Persistence_Status_t App_Persistence_Save(Profile_Storage_Info_t* out)
{
    if (!prv_can_mutate() || !App_Gen_Has_Configured_Signals()) {
        s_last_status = APP_PERSISTENCE_INVALID_STATE;
        return s_last_status;
    }
    /* Phase 6 stores constant/blob state only. A waveform must wait for the
     * banked SET_SIGNAL_MODE format rather than being silently discarded. */
    if (App_Gen_Get_Active_Signal_Count() !=
        J1939_Value_Store_Get_Active_Count()) {
        s_last_status = APP_PERSISTENCE_UNSUPPORTED_CONFIG;
        return s_last_status;
    }

    Profile_Storage_Info_t previous;
    const Profile_Storage_Status_t previous_status =
        Profile_Storage_Open_Latest(&previous);
    const bool autostart = (previous_status == PROFILE_STORAGE_OK) &&
        ((previous.flags & J1939_PROFILE_STORAGE_FLAG_AUTOSTART) != 0u);
    s_last_status = prv_map(Profile_Storage_Save(
        App_Gen_Get_Active_Profile_Id(), App_Gen_Get_Baud() * 1000u,
        autostart, out));
    return s_last_status;
}

App_Persistence_Status_t App_Persistence_Load(uint16_t* out_generation,
                                              uint16_t* out_signal_count,
                                              uint8_t* out_pgn_count)
{
    if (!prv_can_mutate()) {
        s_last_status = APP_PERSISTENCE_INVALID_STATE;
        return s_last_status;
    }
    Profile_Storage_Info_t info;
    Profile_Storage_Status_t storage = Profile_Storage_Open_Latest(&info);
    if (storage != PROFILE_STORAGE_OK) {
        s_last_status = prv_map(storage);
        return s_last_status;
    }
    const uint8_t expected_blobs = info.blob_count;
    if (App_Gen_Begin_Profile_Config(info.scalar_count, expected_blobs,
                                     info.profile_id) != APP_CONFIG_TXN_OK) {
        s_last_status = APP_PERSISTENCE_APPLY_ERROR;
        return s_last_status;
    }

    App_Load_Context_t context = { true };
    storage = Profile_Storage_Visit_Latest(prv_load_scalar, prv_load_blob,
                                           &context, NULL);
    if ((storage != PROFILE_STORAGE_OK) || !context.ok) {
        App_Gen_Abort_Config();
        s_last_status = (storage == PROFILE_STORAGE_OK)
            ? APP_PERSISTENCE_APPLY_ERROR : prv_map(storage);
        return s_last_status;
    }

    if (App_Gen_Commit_Config(out_generation, out_signal_count,
                              out_pgn_count) != APP_CONFIG_TXN_OK) {
        s_last_status = APP_PERSISTENCE_APPLY_ERROR;
        return s_last_status;
    }
    if (App_Gen_Get_Config_Crc() != info.config_crc) {
        s_last_status = APP_PERSISTENCE_VERIFY_ERROR;
        return s_last_status;
    }
    s_last_status = APP_PERSISTENCE_OK;
    return s_last_status;
}

App_Persistence_Status_t App_Persistence_Erase(void)
{
    if (!prv_can_mutate()) {
        s_last_status = APP_PERSISTENCE_INVALID_STATE;
        return s_last_status;
    }
    s_last_status = prv_map(Profile_Storage_Erase());
    return s_last_status;
}

App_Persistence_Status_t App_Persistence_Set_Autostart(
    bool enabled, Profile_Storage_Info_t* out)
{
    if (!prv_can_mutate()) {
        s_last_status = APP_PERSISTENCE_INVALID_STATE;
        return s_last_status;
    }
    s_last_status = prv_map(Profile_Storage_Set_Autostart(enabled, out));
    return s_last_status;
}

App_Persistence_Status_t App_Persistence_Get_Info(App_Persistence_Info_t* out)
{
    if (out == NULL) {
        s_last_status = APP_PERSISTENCE_INVALID_IMAGE;
        return s_last_status;
    }
    memset(out, 0, sizeof(*out));
    const Profile_Storage_Status_t status =
        Profile_Storage_Open_Latest(&out->stored);
    const App_Persistence_Status_t mapped = prv_map(status);
    out->valid = status == PROFILE_STORAGE_OK;
    if (out->valid) {
        out->autostart =
            (out->stored.flags & J1939_PROFILE_STORAGE_FLAG_AUTOSTART) != 0u;
        out->dirty = out->stored.config_crc != App_Gen_Get_Config_Crc();
    }
    out->last_status = s_last_status;
    return mapped;
}
