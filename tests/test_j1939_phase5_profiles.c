/* Phase 5 integration: every generated customer profile is configured through
 * protocol 2.3, exact key membership is enforced, and preflight/status data
 * agrees with the generated compact profile table. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_control_can.h"
#include "app_generator.h"
#include "hal_can.h"
#include "hal_console.h"
#include "hal_led.h"
#include "hal_time.h"
#include "j1939_customer_profiles.h"
#include "j1939_rx_router.h"
#include "j1939_signal_definitions.h"

static uint8_t s_rx_data[8];
static uint8_t s_rx_length;
static bool s_rx_pending;
static uint8_t s_response[8];
static uint32_t s_response_count;
static uint32_t s_now_ms;
static uint8_t s_sequence;

bool hal_can_init(uint32_t bitrate_bps)
{
    (void)bitrate_bps;
    return true;
}

bool hal_can_receive_ext(uint32_t* can_id, uint8_t* data, uint8_t* length)
{
    if (!s_rx_pending) {
        return false;
    }
    *can_id = 0x18EF80F9u;
    *length = s_rx_length;
    memcpy(data, s_rx_data, s_rx_length);
    s_rx_pending = false;
    return true;
}

hal_can_tx_status_t hal_can_send_ext(uint32_t can_id,
                                     const uint8_t* data,
                                     uint8_t length)
{
    assert(can_id == 0x18EFF980u);
    assert(length == 8u);
    memcpy(s_response, data, 8u);
    s_response_count++;
    return HCAN_TX_OK;
}

void hal_can_get_bit_timing(hal_can_bit_timing_t* out) { memset(out, 0, sizeof(*out)); }
void hal_can_get_error(hal_can_error_t* out) { memset(out, 0, sizeof(*out)); }
void hal_can_diagnose(hal_can_diag_t* out) { memset(out, 0, sizeof(*out)); }
uint32_t hal_time_ms(void) { return s_now_ms; }
void hal_led_set(bool on) { (void)on; }
void hal_console_write(const char* str) { (void)str; }
void hal_console_write_line(const char* str) { (void)str; }
void hal_console_write_u32(uint32_t value) { (void)value; }
void hal_console_write_i32(int32_t value) { (void)value; }
void hal_console_write_hex(uint32_t value) { (void)value; }
void hal_console_write_float(float value, uint8_t decimals)
{
    (void)value;
    (void)decimals;
}
uint16_t hal_console_tx_free(void) { return UINT16_MAX; }

static void write_u32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void reset_fixture(void)
{
    memset(s_rx_data, 0, sizeof(s_rx_data));
    memset(s_response, 0, sizeof(s_response));
    s_rx_length = 0u;
    s_rx_pending = false;
    s_response_count = 0u;
    s_now_ms = 0u;
    s_sequence = 1u;
    App_Gen_Init(500u);
    J1939_Rx_Router_Init();
    App_Control_Can_Init();
}

static uint8_t next_sequence(void)
{
    const uint8_t result = s_sequence;
    s_sequence = (s_sequence == 0xFFu) ? 1u : (uint8_t)(s_sequence + 1u);
    return result;
}

static const uint8_t* send(uint8_t frame[8])
{
    assert(!s_rx_pending);
    frame[1] = next_sequence();
    memcpy(s_rx_data, frame, 8u);
    s_rx_length = 8u;
    s_rx_pending = true;
    const uint32_t before = s_response_count;
    J1939_Rx_Router_Poll(1u);
    assert(s_response_count == before + 1u);
    assert(s_response[0] == (uint8_t)(frame[0] | APP_CONTROL_RESPONSE_MASK));
    assert(s_response[1] == frame[1]);
    s_now_ms++;
    return s_response;
}

static const uint8_t* begin_profile(uint8_t profile_id, uint8_t scalars,
                                    uint8_t blobs)
{
    uint8_t frame[8] = { APP_CONTROL_OPCODE_BEGIN_CONFIG, 0u };
    write_u32(&frame[2], J1939_CATALOG_CRC32);
    frame[6] = scalars;
    frame[7] = (uint8_t)((blobs & APP_CONTROL_BEGIN_BLOB_MASK) |
        ((uint8_t)(profile_id + 1u) << APP_CONTROL_BEGIN_PROFILE_SHIFT));
    return send(frame);
}

static const uint8_t* begin_custom(uint8_t scalars, uint8_t blobs)
{
    uint8_t frame[8] = { APP_CONTROL_OPCODE_BEGIN_CONFIG, 0u };
    write_u32(&frame[2], J1939_CATALOG_CRC32);
    frame[6] = scalars;
    frame[7] = blobs;
    return send(frame);
}

static const uint8_t* set_raw(uint16_t key, uint32_t raw)
{
    uint8_t frame[8] = { APP_CONTROL_OPCODE_SET_SIGNAL_RAW, 0u };
    frame[2] = (uint8_t)key;
    frame[3] = (uint8_t)(key >> 8);
    write_u32(&frame[4], raw);
    return send(frame);
}

static void write_vin(void)
{
    static const uint8_t vin[] = "1FUJGLDR5CLBP8834";
    for (uint8_t offset = 0u; offset < 17u; offset = (uint8_t)(offset + 3u)) {
        uint8_t frame[8] = { APP_CONTROL_OPCODE_WRITE_BLOB, 0u,
                             J1939_KEY_VI_SPN237, 17u, offset };
        for (uint8_t i = 0u; (i < 3u) && ((uint8_t)(offset + i) < 17u); i++) {
            frame[5u + i] = vin[offset + i];
        }
        assert(send(frame)[2] == APP_CONTROL_STATUS_OK);
    }
}

static const uint8_t* simple(uint8_t opcode)
{
    uint8_t frame[8] = { 0u };
    frame[0] = opcode;
    return send(frame);
}

static const uint8_t* status_page(uint8_t page)
{
    uint8_t frame[8] = { APP_CONTROL_OPCODE_GET_STATUS, 0u, page };
    return send(frame);
}

static void profile_record_counts(const J1939_Customer_Profile_t* profile,
                                  uint8_t* scalars, uint8_t* blobs)
{
    uint8_t scalar_count = 0u;
    uint8_t blob_count = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[i];
        if (!J1939_Customer_Profile_Has_Key(profile, signal->signal_key)) {
            continue;
        }
        if ((signal->flags & J1939_CAT_SIG_BLOB) != 0u) {
            blob_count++;
        } else {
            scalar_count++;
        }
    }
    *scalars = scalar_count;
    *blobs = blob_count;
}

static void test_every_generated_profile(void)
{
    for (uint8_t profile_id = 0u; profile_id < J1939_PROFILE_COUNT; profile_id++) {
        const J1939_Customer_Profile_t* profile =
            J1939_Customer_Profile_Find(profile_id);
        assert(profile != NULL);
        uint8_t scalars = 0u;
        uint8_t blobs = 0u;
        profile_record_counts(profile, &scalars, &blobs);
        assert((uint16_t)scalars + blobs == profile->signal_count);

        reset_fixture();
        assert(begin_profile(profile_id, scalars, blobs)[2] ==
               APP_CONTROL_STATUS_OK);
        for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
            const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[i];
            if (!J1939_Customer_Profile_Has_Key(profile, signal->signal_key)) {
                continue;
            }
            if ((signal->flags & J1939_CAT_SIG_BLOB) != 0u) {
                write_vin();
            } else {
                assert(set_raw(signal->signal_key, signal->raw_min)[2] ==
                       APP_CONTROL_STATUS_OK);
            }
        }

        const uint8_t* response = simple(APP_CONTROL_OPCODE_COMMIT_CONFIG);
        assert(response[2] == APP_CONTROL_STATUS_OK);
        assert(response[3] == profile->pgn_count);
        assert((read_u32(&response[4]) >> 16) == profile->signal_count);
        assert(App_Gen_Get_Active_Profile_Id() == profile_id);
        assert(App_Gen_Get_Active_Signal_Count() == profile->signal_count);
        assert(App_Gen_Get_Active_PGN_Count() == profile->pgn_count);

        response = status_page(APP_CONTROL_STATUS_PAGE_PROFILE);
        assert(response[2] == APP_CONTROL_STATUS_OK);
        assert(response[3] == profile->flags);
        assert(response[4] == profile_id);
        assert(response[5] == profile->signal_count);
        assert(response[6] == profile->pgn_count);
        assert(response[7] == profile->baseline_load_500k_decipct);

        /* TP profiles (D, M, ALL) start too: FEE1/FEE3 go out with BAM and
         * the On-Request VIN stays silent. */
        response = simple(APP_CONTROL_OPCODE_START);
        assert(response[2] == APP_CONTROL_STATUS_OK);
        assert(App_Gen_Is_Running());
    }
}

static void test_profile_rejections_and_custom_compatibility(void)
{
    const J1939_Customer_Profile_t* profile_c =
        J1939_Customer_Profile_Find(J1939_PROFILE_ID_C);
    uint8_t scalars = 0u;
    uint8_t blobs = 0u;
    profile_record_counts(profile_c, &scalars, &blobs);

    reset_fixture();
    assert(begin_profile(J1939_PROFILE_ID_C, (uint8_t)(scalars - 1u), blobs)[2] ==
           APP_CONTROL_STATUS_PROFILE_MISMATCH);
    assert(!App_Gen_Config_Transaction_Open());

    uint8_t unknown[8] = { APP_CONTROL_OPCODE_BEGIN_CONFIG, 0u };
    write_u32(&unknown[2], J1939_CATALOG_CRC32);
    unknown[7] = (uint8_t)((100u + 1u) << APP_CONTROL_BEGIN_PROFILE_SHIFT);
    assert(send(unknown)[2] == APP_CONTROL_STATUS_UNKNOWN_PROFILE);

    assert(begin_profile(J1939_PROFILE_ID_C, scalars, blobs)[2] ==
           APP_CONTROL_STATUS_OK);
    uint16_t outside_key = 0u;
    for (uint16_t key = 1u; key <= J1939_CATALOG_KEY_MAX; key++) {
        const uint16_t index = J1939_Catalog_Index_From_Key(key);
        if (!J1939_Customer_Profile_Has_Key(profile_c, key) &&
            ((J1939_Catalog_Signals[index].flags & J1939_CAT_SIG_BLOB) == 0u)) {
            outside_key = key;
            break;
        }
    }
    assert(outside_key != 0u);
    assert(set_raw(outside_key,
                   J1939_Catalog_Signals[
                       J1939_Catalog_Index_From_Key(outside_key)].raw_min)[2] ==
           APP_CONTROL_STATUS_PROFILE_KEY_NOT_ALLOWED);
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] ==
           APP_CONTROL_STATUS_TRANSACTION_REJECTED);
    assert(App_Gen_Get_Config_Generation() == 0u);

    /* A legacy BEGIN frame (upper seven bits zero) remains custom engineer mode. */
    assert(begin_custom(1u, 0u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(J1939_KEY_EEC1_SPN190, 12000u)[2] == APP_CONTROL_STATUS_OK);
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Get_Active_Profile_Id() == J1939_PROFILE_ID_CUSTOM);
    const uint8_t* response = status_page(APP_CONTROL_STATUS_PAGE_PROFILE);
    assert(response[3] == 0u);
    assert(response[4] == J1939_PROFILE_ID_CUSTOM);
    assert(response[5] == 1u);
    assert(response[6] == 1u);
    assert(response[7] == 0xFFu);
}

int main(void)
{
    test_every_generated_profile();
    test_profile_rejections_and_custom_compatibility();
    puts("Phase 5 customer-profile integration tests: PASS (11 profiles)");
    return 0;
}
