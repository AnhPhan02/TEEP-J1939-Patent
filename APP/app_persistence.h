#ifndef APP_PERSISTENCE_H
#define APP_PERSISTENCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "j1939_profile_storage.h"

typedef enum {
    APP_PERSISTENCE_OK = 0,
    APP_PERSISTENCE_EMPTY,
    APP_PERSISTENCE_INVALID_STATE,
    APP_PERSISTENCE_UNSUPPORTED_CONFIG,
    APP_PERSISTENCE_CATALOG_MISMATCH,
    APP_PERSISTENCE_INVALID_IMAGE,
    APP_PERSISTENCE_FLASH_ERROR,
    APP_PERSISTENCE_VERIFY_ERROR,
    APP_PERSISTENCE_APPLY_ERROR
} App_Persistence_Status_t;

typedef struct {
    bool valid;
    bool autostart;
    bool dirty;
    Profile_Storage_Info_t stored;
    App_Persistence_Status_t last_status;
} App_Persistence_Info_t;

void App_Persistence_Init(void);
App_Persistence_Status_t App_Persistence_Save(Profile_Storage_Info_t* out);
App_Persistence_Status_t App_Persistence_Load(uint16_t* out_generation,
                                              uint16_t* out_signal_count,
                                              uint8_t* out_pgn_count);
App_Persistence_Status_t App_Persistence_Erase(void);
App_Persistence_Status_t App_Persistence_Set_Autostart(
    bool enabled, Profile_Storage_Info_t* out);
App_Persistence_Status_t App_Persistence_Get_Info(App_Persistence_Info_t* out);

#ifdef __cplusplus
}
#endif
#endif /* APP_PERSISTENCE_H */
