#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_control_can.h"
#include "app_generator.h"
#include "app_persistence.h"
#include "hal_can.h"
#include "hal_console.h"
#include "hal_flash.h"
#include "hal_flash_fake.h"
#include "hal_led.h"
#include "hal_time.h"
#include "j1939_customer_profiles.h"
#include "j1939_rx_router.h"
#include "j1939_signal_definitions.h"
#include "j1939_value_store.h"

static uint8_t s_rx[8];
static bool s_rx_pending;
static uint8_t s_tx[8];
static uint32_t s_tx_count;
static uint32_t s_now;

bool hal_can_init(uint32_t bitrate_bps) { (void)bitrate_bps; return true; }
bool hal_can_receive_ext(uint32_t* can_id, uint8_t* data, uint8_t* length)
{
    if (!s_rx_pending) return false;
    *can_id = 0x18EF80F9u;
    *length = 8u;
    memcpy(data, s_rx, 8u);
    s_rx_pending = false;
    return true;
}
hal_can_tx_status_t hal_can_send_ext(uint32_t can_id, const uint8_t* data,
                                     uint8_t length)
{
    assert(can_id == 0x18EFF980u && length == 8u);
    memcpy(s_tx, data, 8u);
    s_tx_count++;
    return HCAN_TX_OK;
}
void hal_can_get_bit_timing(hal_can_bit_timing_t* out) { memset(out, 0, sizeof(*out)); }
void hal_can_get_error(hal_can_error_t* out) { memset(out, 0, sizeof(*out)); }
void hal_can_diagnose(hal_can_diag_t* out) { memset(out, 0, sizeof(*out)); }
uint32_t hal_time_ms(void) { return s_now; }
void hal_led_set(bool on) { (void)on; }
void hal_console_write(const char* value) { (void)value; }
void hal_console_write_line(const char* value) { (void)value; }
void hal_console_write_u32(uint32_t value) { (void)value; }
void hal_console_write_i32(int32_t value) { (void)value; }
void hal_console_write_hex(uint32_t value) { (void)value; }
void hal_console_write_float(float value, uint8_t decimals)
{ (void)value; (void)decimals; }
uint16_t hal_console_tx_free(void) { return UINT16_MAX; }

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint8_t* send_control(const uint8_t frame[8])
{
    const uint32_t before = s_tx_count;
    memcpy(s_rx, frame, 8u);
    s_rx_pending = true;
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == before + 1u);
    return s_tx;
}

static void reset_ram(void)
{
    s_rx_pending = false;
    s_tx_count = 0u;
    s_now = 0u;
    App_Gen_Init(500u);
    App_Persistence_Init();
    J1939_Rx_Router_Init();
    App_Control_Can_Init();
}

static void profile_counts(const J1939_Customer_Profile_t* profile,
                           uint16_t* scalars, uint8_t* blobs)
{
    *scalars = 0u;
    *blobs = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[i];
        if (!J1939_Customer_Profile_Has_Key(profile, signal->signal_key)) continue;
        if ((signal->flags & J1939_CAT_SIG_BLOB) != 0u) (*blobs)++;
        else (*scalars)++;
    }
}

static void configure_profile(uint8_t profile_id)
{
    const J1939_Customer_Profile_t* profile =
        J1939_Customer_Profile_Find(profile_id);
    uint16_t scalars;
    uint8_t blobs;
    profile_counts(profile, &scalars, &blobs);
    assert(blobs == 0u);
    assert(App_Gen_Begin_Profile_Config(scalars, blobs, profile_id) ==
           APP_CONFIG_TXN_OK);
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[i];
        if (J1939_Customer_Profile_Has_Key(profile, signal->signal_key)) {
            assert(App_Gen_Stage_Constant_Raw(signal->signal_key,
                                              signal->raw_min, NULL) ==
                   APP_CONFIG_TXN_OK);
        }
    }
    assert(App_Gen_Commit_Config(NULL, NULL, NULL) == APP_CONFIG_TXN_OK);
}

static void test_save_load_and_fallback(void)
{
    Test_Flash_Reset();
    reset_ram();
    configure_profile(J1939_PROFILE_ID_C);
    const uint32_t crc1 = App_Gen_Get_Config_Crc();
    Profile_Storage_Info_t saved1;
    assert(App_Persistence_Save(&saved1) == APP_PERSISTENCE_OK);
    assert(saved1.slot == 0u && saved1.sequence == 1u && saved1.config_crc == crc1);

    App_Gen_Clear();
    assert(App_Persistence_Load(NULL, NULL, NULL) == APP_PERSISTENCE_OK);
    assert(App_Gen_Get_Active_Profile_Id() == J1939_PROFILE_ID_C);
    assert(App_Gen_Get_Config_Crc() == crc1);

    uint16_t generation;
    uint16_t affected;
    uint16_t excluded;
    assert(App_Gen_Set_Global_Percent(10000u, &generation, &affected,
                                     &excluded) == APP_CONFIG_TXN_OK);
    const uint32_t crc2 = App_Gen_Get_Config_Crc();
    assert(crc2 != crc1);
    Profile_Storage_Info_t saved2;
    assert(App_Persistence_Save(&saved2) == APP_PERSISTENCE_OK);
    assert(saved2.slot == 1u && saved2.sequence == 2u && saved2.config_crc == crc2);

    Test_Flash_Corrupt(HAL_FLASH_PROFILE_SLOT_B_ADDRESS + 40u, 0u);
    Profile_Storage_Info_t fallback;
    assert(Profile_Storage_Open_Latest(&fallback) == PROFILE_STORAGE_OK);
    assert(fallback.slot == 0u && fallback.config_crc == crc1);
    Test_Flash_Corrupt(HAL_FLASH_PROFILE_SLOT_A_ADDRESS + 40u, 0u);
    assert(Profile_Storage_Open_Latest(&fallback) == PROFILE_STORAGE_EMPTY);
}

static void test_invalid_states_and_waveform_rejection(void)
{
    Test_Flash_Reset();
    reset_ram();
    configure_profile(J1939_PROFILE_ID_C);
    App_Gen_Start(0u, J1939_SCHED_MODE_SAE);
    assert(App_Gen_Is_Running());
    assert(App_Persistence_Save(NULL) == APP_PERSISTENCE_INVALID_STATE);
    App_Gen_Stop();

    assert(App_Gen_Begin_Config(1u, 0u) == APP_CONFIG_TXN_OK);
    assert(App_Persistence_Save(NULL) == APP_PERSISTENCE_INVALID_STATE);
    App_Gen_Abort_Config();

    App_Gen_Clear();
    App_Gen_Load_Defaults();
#if ENABLE_WAVEFORM
    assert(App_Persistence_Save(NULL) ==
           APP_PERSISTENCE_UNSUPPORTED_CONFIG);
#else
    /* Constant-only build: the demo defaults are three plain constants. */
    assert(App_Gen_Get_Active_Signal_Count() == 3u);
    assert(J1939_Value_Store_Get_Active_Count() == 3u);
    assert(App_Persistence_Save(NULL) == APP_PERSISTENCE_OK);

    const App_Signal_Request_t sine = {
        .pgn = 0xF004u, .spn = 190u, .type = PATTERN_SINE,
        .min_value = 800.0f, .max_value = 3500.0f, .param1 = 8.0f
    };
    assert(App_Gen_Config_Signal(&sine, NULL) == APP_CFG_UNSUPPORTED_SIGNAL);
    assert(App_Gen_Get_Active_Signal_Count() == 3u);
    const uint8_t limits[8] = {
        APP_CONTROL_OPCODE_GET_STATUS, 90u, APP_CONTROL_STATUS_PAGE_LIMITS
    };
    assert(send_control(limits)[6] == 0u);         /* waveform pool */
#endif
}

static void test_power_cut_preserves_old_or_new(void)
{
    uint8_t baseline[TEST_FLASH_BYTES];
    Test_Flash_Reset();
    reset_ram();
    configure_profile(J1939_PROFILE_ID_C);
    Profile_Storage_Info_t first;
    assert(App_Persistence_Save(&first) == APP_PERSISTENCE_OK);
    const uint32_t crc1 = first.config_crc;
    Test_Flash_Snapshot(baseline);

    uint16_t generation, affected, excluded;
    assert(App_Gen_Set_Global_Percent(10000u, &generation, &affected,
                                     &excluded) == APP_CONFIG_TXN_OK);
    const uint32_t crc2 = App_Gen_Get_Config_Crc();
    Test_Flash_Restore(baseline);
    Profile_Storage_Info_t second;
    assert(App_Persistence_Save(&second) == APP_PERSISTENCE_OK);
    const uint32_t mutations = Test_Flash_Get_Mutation_Count();
    assert(second.config_crc == crc2 && mutations > 4u);

    for (uint32_t cut = 0u; cut < mutations; cut++) {
        Test_Flash_Restore(baseline);
        Test_Flash_Set_Fail_After((int32_t)cut);
        (void)App_Persistence_Save(NULL);
        Test_Flash_Set_Fail_After(-1);
        Profile_Storage_Info_t recovered;
        assert(Profile_Storage_Open_Latest(&recovered) == PROFILE_STORAGE_OK);
        assert((recovered.config_crc == crc1) || (recovered.config_crc == crc2));
    }
}

static void test_autostart_erase_and_protocol_dedup(void)
{
    Test_Flash_Reset();
    reset_ram();
    configure_profile(J1939_PROFILE_ID_C);
    Profile_Storage_Info_t info;
    assert(App_Persistence_Save(&info) == APP_PERSISTENCE_OK);
    assert(App_Persistence_Set_Autostart(true, &info) == APP_PERSISTENCE_OK);
    assert((info.flags & J1939_PROFILE_STORAGE_FLAG_AUTOSTART) != 0u);

    uint8_t frame[8] = { APP_CONTROL_OPCODE_SAVE_CONFIG, 77u };
    assert(send_control(frame)[2] == APP_CONTROL_STATUS_OK);
    const uint32_t after_first = Test_Flash_Get_Mutation_Count();
    assert(send_control(frame)[2] == APP_CONTROL_STATUS_OK);
    assert(Test_Flash_Get_Mutation_Count() == after_first);

    const uint8_t page5[8] = {
        APP_CONTROL_OPCODE_GET_STATUS, 78u,
        APP_CONTROL_STATUS_PAGE_STORAGE, 0u, 0u, 0u, 0u, 0u
    };
    const uint8_t* response = send_control(page5);
    assert(response[2] == APP_CONTROL_STATUS_OK);
    assert((response[3] & (APP_CONTROL_STORAGE_VALID |
                           APP_CONTROL_STORAGE_AUTOSTART)) ==
           (APP_CONTROL_STORAGE_VALID | APP_CONTROL_STORAGE_AUTOSTART));
    assert(read_u32(&response[4]) == App_Gen_Get_Config_Crc());

    uint8_t autostart_off[8] = {
        APP_CONTROL_OPCODE_SET_AUTOSTART, 79u, 0u, 0u, 0u, 0u, 0u, 0u
    };
    assert(send_control(autostart_off)[2] == APP_CONTROL_STATUS_OK);
    assert(Profile_Storage_Open_Latest(&info) == PROFILE_STORAGE_OK);
    assert((info.flags & J1939_PROFILE_STORAGE_FLAG_AUTOSTART) == 0u);

    App_Gen_Clear();
    const uint8_t load[8] = {
        APP_CONTROL_OPCODE_LOAD_SAVED_CONFIG, 80u, 0u, 0u, 0u, 0u, 0u, 0u
    };
    assert(send_control(load)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Get_Active_Profile_Id() == J1939_PROFILE_ID_C);

    const uint8_t erase[8] = {
        APP_CONTROL_OPCODE_ERASE_SAVED_CONFIG, 81u, 0u, 0u, 0u, 0u, 0u, 0u
    };
    assert(send_control(erase)[2] == APP_CONTROL_STATUS_OK);
    assert(Profile_Storage_Open_Latest(&info) == PROFILE_STORAGE_EMPTY);
}

static void test_repeated_save_sequence(void)
{
    Test_Flash_Reset();
    reset_ram();
    configure_profile(J1939_PROFILE_ID_C);
    for (uint32_t i = 0u; i < 100u; i++) {
        uint16_t generation, affected, excluded;
        assert(App_Gen_Set_Global_Percent((i & 1u) ? 10000u : 0u,
                                         &generation, &affected,
                                         &excluded) == APP_CONFIG_TXN_OK);
        Profile_Storage_Info_t info;
        assert(App_Persistence_Save(&info) == APP_PERSISTENCE_OK);
        assert(info.sequence == i + 1u);
        assert(info.slot == (uint8_t)(i & 1u));
    }
}

int main(void)
{
    test_save_load_and_fallback();
    test_power_cut_preserves_old_or_new();
    test_invalid_states_and_waveform_rejection();
    test_autostart_erase_and_protocol_dedup();
    test_repeated_save_sequence();
    puts("Phase 6 A/B persistence and power-cut tests: PASS");
    return 0;
}
