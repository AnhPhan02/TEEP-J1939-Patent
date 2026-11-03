#include "app_persistence.h"
#include <string.h>

void App_Persistence_Init(void) {}
App_Persistence_Status_t App_Persistence_Save(Profile_Storage_Info_t* out)
{ (void)out; return APP_PERSISTENCE_EMPTY; }
App_Persistence_Status_t App_Persistence_Load(uint16_t* generation,
                                              uint16_t* count, uint8_t* pgns)
{ (void)generation; (void)count; (void)pgns; return APP_PERSISTENCE_EMPTY; }
App_Persistence_Status_t App_Persistence_Erase(void)
{ return APP_PERSISTENCE_OK; }
App_Persistence_Status_t App_Persistence_Set_Autostart(
    bool enabled, Profile_Storage_Info_t* out)
{ (void)enabled; (void)out; return APP_PERSISTENCE_EMPTY; }
App_Persistence_Status_t App_Persistence_Get_Info(App_Persistence_Info_t* out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->last_status = APP_PERSISTENCE_EMPTY;
    }
    return APP_PERSISTENCE_EMPTY;
}
