/* Phase 4B host integration test: WRITE_BLOB (VIN), SET_GLOBAL_PERCENT,
 * configuration CRC and the GET_STATUS limits/features page, exercised end to
 * end through the proprietary CAN control protocol. */

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
#include "j1939_rx_router.h"
#include "j1939_signal_definitions.h"
#include "j1939_value_store.h"

#define TEST_FRAME_CAPACITY  512u
#define VIN_KEY              J1939_KEY_VI_SPN237
#define EEC1_SPEED_KEY       J1939_KEY_EEC1_SPN190
#define EEC2_PEDAL_KEY       J1939_KEY_EEC2_SPN91
#define EEC1_CAN_ID          0x0CF00400u
#define EEC2_CAN_ID          0x0CF00300u

typedef struct {
    uint32_t can_id;
    uint8_t data[8];
    uint8_t length;
} Test_Frame_t;

static Test_Frame_t s_rx[TEST_FRAME_CAPACITY];
static Test_Frame_t s_tx[TEST_FRAME_CAPACITY];
static uint16_t s_rx_count;
static uint16_t s_rx_index;
static uint16_t s_tx_count;
static uint32_t s_now_ms;
static uint8_t s_sequence;

bool hal_can_init(uint32_t bitrate_bps)
{
    (void)bitrate_bps;
    return true;
}

bool hal_can_receive_ext(uint32_t* can_id, uint8_t* data, uint8_t* length)
{
    if (s_rx_index >= s_rx_count) {
        return false;
    }
    const Test_Frame_t* frame = &s_rx[s_rx_index++];
    *can_id = frame->can_id;
    *length = frame->length;
    memcpy(data, frame->data, frame->length);
    return true;
}

hal_can_tx_status_t hal_can_send_ext(uint32_t can_id,
                                     const uint8_t* data,
                                     uint8_t length)
{
    assert(s_tx_count < TEST_FRAME_CAPACITY);
    Test_Frame_t* frame = &s_tx[s_tx_count++];
    frame->can_id = can_id;
    frame->length = length;
    memcpy(frame->data, data, length);
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

/* ---- helpers ------------------------------------------------------------- */

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void reset_fixture(void)
{
    memset(s_rx, 0, sizeof(s_rx));
    memset(s_tx, 0, sizeof(s_tx));
    s_rx_count = 0u;
    s_rx_index = 0u;
    s_tx_count = 0u;
    s_now_ms = 0u;
    s_sequence = 1u;
    App_Gen_Init(500u);
    J1939_Rx_Router_Init();
    App_Control_Can_Init();
}

static uint8_t next_sequence(void)
{
    const uint8_t sequence = s_sequence;
    s_sequence = (uint8_t)((s_sequence == 0xFFu) ? 1u : (s_sequence + 1u));
    return sequence;
}

/* Sends one request (new sequence) and returns the response bytes. */
static const uint8_t* send_frame(const uint8_t request[8])
{
    assert(s_rx_count < TEST_FRAME_CAPACITY);
    Test_Frame_t* frame = &s_rx[s_rx_count++];
    frame->can_id = 0x18EF80F9u;
    frame->length = 8u;
    memcpy(frame->data, request, 8u);
    const uint16_t tx_before = s_tx_count;
    J1939_Rx_Router_Poll(1u);
    assert(s_tx_count == (uint16_t)(tx_before + 1u));
    assert(s_tx[s_tx_count - 1u].can_id == 0x18EFF980u);
    assert(s_tx[s_tx_count - 1u].data[1] == request[1]);
    return s_tx[s_tx_count - 1u].data;
}

static void make(uint8_t frame[8], uint8_t opcode)
{
    memset(frame, 0, 8u);
    frame[0] = opcode;
    frame[1] = next_sequence();
}

static const uint8_t* begin(uint8_t scalars, uint8_t blobs)
{
    uint8_t frame[8];
    make(frame, APP_CONTROL_OPCODE_BEGIN_CONFIG);
    write_u32(&frame[2], J1939_CATALOG_CRC32);
    frame[6] = scalars;
    frame[7] = blobs;
    return send_frame(frame);
}

static const uint8_t* set_raw(uint16_t key, uint32_t raw)
{
    uint8_t frame[8];
    make(frame, APP_CONTROL_OPCODE_SET_SIGNAL_RAW);
    frame[2] = (uint8_t)key;
    frame[3] = (uint8_t)(key >> 8);
    write_u32(&frame[4], raw);
    return send_frame(frame);
}

static void make_blob(uint8_t frame[8], uint16_t key, uint8_t total,
                      uint8_t offset, const uint8_t* bytes)
{
    make(frame, APP_CONTROL_OPCODE_WRITE_BLOB);
    frame[2] = (uint8_t)key;
    frame[3] = total;
    frame[4] = offset;
    for (uint8_t i = 0u; (i < 3u) && ((uint8_t)(offset + i) < total); i++) {
        frame[5u + i] = bytes[offset + i];
    }
}

static const uint8_t* write_blob(uint16_t key, uint8_t total, uint8_t offset,
                                 const uint8_t* bytes)
{
    uint8_t frame[8];
    make_blob(frame, key, total, offset, bytes);
    return send_frame(frame);
}

static const uint8_t* simple(uint8_t opcode)
{
    uint8_t frame[8];
    make(frame, opcode);
    return send_frame(frame);
}

static const uint8_t* get_status(uint8_t page)
{
    uint8_t frame[8];
    make(frame, APP_CONTROL_OPCODE_GET_STATUS);
    frame[2] = page;
    return send_frame(frame);
}

static const uint8_t* global_percent(uint16_t bp)
{
    uint8_t frame[8];
    make(frame, APP_CONTROL_OPCODE_SET_GLOBAL_PERCENT);
    frame[2] = (uint8_t)bp;
    frame[3] = (uint8_t)(bp >> 8);
    return send_frame(frame);
}

static void write_whole_blob(uint16_t key, uint8_t total, const uint8_t* bytes)
{
    for (uint8_t offset = 0u; offset < total; offset = (uint8_t)(offset + 3u)) {
        const uint8_t* r = write_blob(key, total, offset, bytes);
        assert(r[2] == APP_CONTROL_STATUS_OK);
        assert(r[3] == total);
    }
}

/* Independent configuration CRC (contract in j1939_value_store.h). */
static uint32_t crc_bytes(uint32_t crc, const uint8_t* data, uint32_t length)
{
    for (uint32_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static uint32_t crc_le(uint32_t crc, uint32_t value, uint8_t bytes)
{
    uint8_t buf[4];
    write_u32(buf, value);
    return crc_bytes(crc, buf, bytes);
}

static uint32_t expected_config_crc(void)
{
    uint32_t crc = crc_bytes(0xFFFFFFFFu, (const uint8_t*)"J1939CFG", 8u);
    crc = crc_le(crc, J1939_CATALOG_CRC32, 4u);
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        if (!J1939_Value_Store_Is_Active(i)) {
            continue;
        }
        crc = crc_le(crc, J1939_Catalog_Signals[i].signal_key, 2u);
        const uint8_t* blob = NULL;
        uint16_t length = 0u;
        uint32_t raw = 0u;
        if (J1939_Value_Store_Get_Blob(i, &blob, &length)) {
            crc = crc_le(crc, length, 2u);
            crc = crc_bytes(crc, blob, length);
        } else {
            assert(J1939_Value_Store_Get_Raw(i, &raw));
            crc = crc_le(crc, raw, 4u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t active_raw(uint16_t key)
{
    uint32_t raw = 0u;
    assert(J1939_Value_Store_Get_Raw(J1939_Catalog_Index_From_Key(key), &raw));
    return raw;
}

static const Test_Frame_t* last_frame(uint32_t can_id)
{
    for (uint16_t i = s_tx_count; i > 0u; i--) {
        if (s_tx[i - 1u].can_id == can_id) {
            return &s_tx[i - 1u];
        }
    }
    return NULL;
}

/* ---- tests --------------------------------------------------------------- */

static void test_protocol_version_and_limits_page(void)
{
    reset_fixture();
    const uint8_t* r = get_status(APP_CONTROL_STATUS_PAGE_PROTOCOL);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert(r[4] == 2u && r[5] == 5u);              /* protocol 2.5 */

    r = get_status(APP_CONTROL_STATUS_PAGE_LIMITS);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert(r[3] == APP_CONTROL_FEATURES);
    assert((r[3] & APP_CONTROL_FEATURE_WRITE_BLOB) != 0u);
    assert((r[3] & APP_CONTROL_FEATURE_GLOBAL_PERCENT) != 0u);
    assert((r[3] & APP_CONTROL_FEATURE_CONFIG_CRC) != 0u);
    assert((r[3] & APP_CONTROL_FEATURE_PERSISTENCE) != 0u);
    assert((r[3] & APP_CONTROL_FEATURE_TRANSPORT_TP) != 0u);   /* BAM TX */
    assert((r[3] & (APP_CONTROL_FEATURE_SIGNAL_MODE |
                    APP_CONTROL_FEATURE_REQUEST_PGN)) == 0u);
    assert(r[4] == J1939_PROFILE_SIGNAL_CAPACITY);
    assert(r[5] == J1939_PROFILE_PGN_CAPACITY);
    assert(r[6] == J1939_WAVEFORM_POOL_CAPACITY);
    assert(r[7] == 200u);

    /* Empty configuration CRC is the header-only CRC. */
    r = get_status(APP_CONTROL_STATUS_PAGE_CONFIG);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert(read_u32(&r[4]) == expected_config_crc());
    assert(read_u32(&r[4]) == App_Gen_Get_Config_Crc());
}

static void test_write_blob_vin_commit(void)
{
    static const uint8_t vin[] = "1FUJGLDR5CLBP8834";   /* 17 ASCII bytes */
    const uint8_t vin_len = 17u;
    uint8_t frame[8];
    reset_fixture();

    assert(begin(1u, 1u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(EEC1_SPEED_KEY, 12000u)[2] == APP_CONTROL_STATUS_OK);

    /* Chunks out of order; an exact retry replays without a second write. */
    const uint8_t* r = write_blob(VIN_KEY, vin_len, 15u, vin);
    assert(r[2] == APP_CONTROL_STATUS_OK && read_u32(&r[4]) == 2u);
    make_blob(frame, VIN_KEY, vin_len, 0u, vin);
    r = send_frame(frame);
    assert(r[2] == APP_CONTROL_STATUS_OK && read_u32(&r[4]) == 5u);
    r = send_frame(frame);
    assert(r[2] == APP_CONTROL_STATUS_OK && read_u32(&r[4]) == 5u);
    for (uint8_t offset = 3u; offset < 15u; offset = (uint8_t)(offset + 3u)) {
        assert(write_blob(VIN_KEY, vin_len, offset, vin)[2] == APP_CONTROL_STATUS_OK);
    }
    assert(App_Gen_Get_Config_Generation() == 0u);  /* nothing active yet */

    r = simple(APP_CONTROL_OPCODE_COMMIT_CONFIG);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert((read_u32(&r[4]) & 0xFFFFu) == 1u);
    assert((read_u32(&r[4]) >> 16) == 2u);          /* scalar + blob */
    assert(r[3] == 2u);                             /* EEC1 + VI PGNs */

    const uint8_t* blob = NULL;
    uint16_t length = 0u;
    assert(J1939_Value_Store_Get_Blob(J1939_Catalog_Index_From_Key(VIN_KEY),
                                      &blob, &length));
    assert(length == vin_len);
    assert(memcmp(blob, vin, vin_len) == 0);
    uint32_t raw = 0u;
    assert(!J1939_Value_Store_Get_Raw(J1939_Catalog_Index_From_Key(VIN_KEY), &raw));

    r = get_status(APP_CONTROL_STATUS_PAGE_CONFIG);
    assert(read_u32(&r[4]) == expected_config_crc());

    /* FEEC is On-Request: START is allowed and VIN stays silent until the
     * Request service exists. */
    assert(simple(APP_CONTROL_OPCODE_START)[2] == APP_CONTROL_STATUS_OK);
    assert(simple(APP_CONTROL_OPCODE_STOP)[2] == APP_CONTROL_STATUS_OK);

    /* Maximum 200-byte VIN (67 chunks) replaces the 17-byte one atomically. */
    uint8_t long_vin[200];
    for (uint16_t i = 0u; i < sizeof(long_vin); i++) {
        long_vin[i] = (uint8_t)('A' + (i % 26u));
    }
    assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
    write_whole_blob(VIN_KEY, 200u, long_vin);
    assert(J1939_Value_Store_Get_Blob(J1939_Catalog_Index_From_Key(VIN_KEY),
                                      &blob, &length) && length == vin_len);
    r = simple(APP_CONTROL_OPCODE_COMMIT_CONFIG);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert(J1939_Value_Store_Get_Blob(J1939_Catalog_Index_From_Key(VIN_KEY),
                                      &blob, &length));
    assert(length == 200u && memcmp(blob, long_vin, 200u) == 0);
    assert(App_Gen_Get_Active_Signal_Count() == 1u);   /* EEC1 not restaged */
}

static void test_write_blob_failures_keep_active(void)
{
    static const uint8_t vin[] = "WDB9634031L123456";
    const uint8_t vin_len = 17u;
    uint8_t frame[8];
    reset_fixture();

    assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
    write_whole_blob(VIN_KEY, vin_len, vin);
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    const uint16_t generation = App_Gen_Get_Config_Generation();
    const uint32_t crc = App_Gen_Get_Config_Crc();

    /* Missing middle chunk -> BLOB_INCOMPLETE, old VIN stays active. */
    assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
    for (uint8_t offset = 0u; offset < vin_len; offset = (uint8_t)(offset + 3u)) {
        if (offset != 6u) {
            assert(write_blob(VIN_KEY, vin_len, offset,
                              (const uint8_t*)"XXXXXXXXXXXXXXXXX")[2] == APP_CONTROL_STATUS_OK);
        }
    }
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] ==
           APP_CONTROL_STATUS_BLOB_INCOMPLETE);
    assert(App_Gen_Get_Config_Generation() == generation);
    assert(App_Gen_Get_Config_Crc() == crc);

    /* Each malformed chunk poisons its transaction. */
    struct {
        uint16_t key;
        uint8_t total;
        uint8_t offset;
        uint8_t pad;
        uint8_t status;
    } const bad[] = {
        { VIN_KEY,        17u,  15u, 0x41u, APP_CONTROL_STATUS_BAD_ARGUMENT },  /* pad != 0 */
        { VIN_KEY,        17u,  17u, 0u,    APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { VIN_KEY,        201u, 0u,  0u,    APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { VIN_KEY,        0u,   0u,  0u,    APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { EEC1_SPEED_KEY, 17u,  0u,  0u,    APP_CONTROL_STATUS_BAD_ARGUMENT },  /* scalar */
        { 0u,             17u,  0u,  0u,    APP_CONTROL_STATUS_UNKNOWN_SIGNAL_KEY },
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
        make_blob(frame, bad[i].key, bad[i].total, bad[i].offset, vin);
        frame[7] = (uint8_t)(frame[7] | bad[i].pad);
        assert(send_frame(frame)[2] == bad[i].status);
        assert(App_Gen_Config_Transaction_Rejected());
        assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] ==
               APP_CONTROL_STATUS_TRANSACTION_REJECTED);
        assert(App_Gen_Get_Config_Generation() == generation);
    }

    /* Changing total_length inside one transaction is rejected. */
    assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
    assert(write_blob(VIN_KEY, 17u, 0u, vin)[2] == APP_CONTROL_STATUS_OK);
    assert(write_blob(VIN_KEY, 18u, 3u, vin)[2] == APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE);
    assert(simple(APP_CONTROL_OPCODE_ABORT_CONFIG)[2] == APP_CONTROL_STATUS_OK);

    /* Declared blob count must match; the generator still refuses more blobs
     * than its single slot. Protocol 2.2 uses byte 7 upper bits for the
     * customer-profile selector, so an impossible count is tested directly. */
    assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] ==
           APP_CONTROL_STATUS_COUNT_MISMATCH);
    assert(App_Gen_Begin_Config(0u, 2u) == APP_CONFIG_TXN_RESOURCE_FULL);
    assert(!App_Gen_Config_Transaction_Open());

    /* The VIN key is still not accepted through the scalar path. */
    assert(begin(1u, 0u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(VIN_KEY, 0x41u)[2] == APP_CONTROL_STATUS_BAD_ARGUMENT);
    assert(simple(APP_CONTROL_OPCODE_ABORT_CONFIG)[2] == APP_CONTROL_STATUS_OK);

    const uint8_t* blob = NULL;
    uint16_t length = 0u;
    assert(J1939_Value_Store_Get_Blob(J1939_Catalog_Index_From_Key(VIN_KEY),
                                      &blob, &length));
    assert(length == vin_len && memcmp(blob, vin, vin_len) == 0);
    assert(App_Gen_Get_Config_Generation() == generation);
}

static void test_global_percent_mapping(void)
{
    /* Contract examples from analyze_spn_data.md 12.3. */
    assert(App_Gen_Global_Percent_To_Raw(0u, 3u, 5000u) == 2u);
    assert(App_Gen_Global_Percent_To_Raw(0u, 10000u, 5000u) == 5000u);
    assert(App_Gen_Global_Percent_To_Raw(0u, 250u, 0u) == 0u);
    assert(App_Gen_Global_Percent_To_Raw(0u, 250u, 10000u) == 250u);
    assert(App_Gen_Global_Percent_To_Raw(0u, 250u, 1u) == 0u);
    assert(App_Gen_Global_Percent_To_Raw(0u, 250u, 20u) == 1u);       /* 0.5 rounds up */
    assert(App_Gen_Global_Percent_To_Raw(0u, 4211081215u, 5000u) == 2105540608u);
    assert(App_Gen_Global_Percent_To_Raw(0u, 4211081215u, 9999u) == 4210660107u);
    assert(App_Gen_Global_Percent_To_Raw(7u, 7u, 5000u) == 7u);
}

static void test_global_percent_full_profile(void)
{
    reset_fixture();
    uint16_t scalar_count = 0u;
    uint16_t global_count = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        if ((sig->flags & J1939_CAT_SIG_BLOB) == 0u) {
            scalar_count++;
            global_count = (uint16_t)(global_count +
                ((sig->flags & J1939_CAT_SIG_GLOBAL_CONTROL) != 0u));
        }
    }
    assert(scalar_count == 178u && global_count == 111u);

    /* All 179 records: 178 scalars at raw_min plus the VIN blob. */
    static const uint8_t vin[] = "1FUJGLDR5CLBP8834";
    assert(begin((uint8_t)scalar_count, 1u)[2] == APP_CONTROL_STATUS_OK);
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        if ((sig->flags & J1939_CAT_SIG_BLOB) == 0u) {
            assert(set_raw(sig->signal_key, sig->raw_min)[2] == APP_CONTROL_STATUS_OK);
        }
    }
    write_whole_blob(VIN_KEY, 17u, vin);
    const uint8_t* r = simple(APP_CONTROL_OPCODE_COMMIT_CONFIG);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert((read_u32(&r[4]) >> 16) == 179u);
    assert(App_Gen_Get_Active_Signal_Count() == 179u);
    const uint16_t generation = App_Gen_Get_Config_Generation();
    const uint32_t crc_min = App_Gen_Get_Config_Crc();
    assert(crc_min == expected_config_crc());

    /* Invalid requests change nothing. */
    assert(global_percent(10001u)[2] == APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE);
    uint8_t frame[8];
    make(frame, APP_CONTROL_OPCODE_SET_GLOBAL_PERCENT);
    frame[2] = 0x88u;
    frame[3] = 0x13u;
    frame[7] = 1u;
    assert(send_frame(frame)[2] == APP_CONTROL_STATUS_BAD_ARGUMENT);
    assert(App_Gen_Get_Config_Generation() == generation);

    /* 50.00 %: one generation, every eligible record mapped, others intact. */
    make(frame, APP_CONTROL_OPCODE_SET_GLOBAL_PERCENT);
    frame[2] = (uint8_t)5000u;
    frame[3] = (uint8_t)(5000u >> 8);
    r = send_frame(frame);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert(r[3] == (uint8_t)(179u - global_count));         /* excluded */
    assert((read_u32(&r[4]) & 0xFFFFu) == (uint16_t)(generation + 1u));
    assert((read_u32(&r[4]) >> 16) == global_count);         /* affected */
    assert(App_Gen_Get_Config_Generation() == (uint16_t)(generation + 1u));
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[i];
        if ((sig->flags & J1939_CAT_SIG_BLOB) != 0u) {
            continue;
        }
        const uint32_t expected = ((sig->flags & J1939_CAT_SIG_GLOBAL_CONTROL) != 0u)
            ? (uint32_t)(sig->raw_min +
                         (((uint64_t)5000u * (sig->raw_max - sig->raw_min) + 5000u) /
                          10000u))
            : sig->raw_min;
        assert(active_raw(sig->signal_key) == expected);
    }
    const uint32_t crc_half = App_Gen_Get_Config_Crc();
    assert(crc_half != crc_min);
    assert(crc_half == expected_config_crc());

    /* Exact retry replays the ACK and does not apply a second generation. */
    uint8_t first[8];
    memcpy(first, s_tx[s_tx_count - 1u].data, sizeof(first));
    assert(memcmp(send_frame(frame), first, sizeof(first)) == 0);
    assert(App_Gen_Get_Config_Generation() == (uint16_t)(generation + 1u));

    /* Back to 0 %: generation advances again, CRC returns to the same value as
     * the original configuration (CRC does not depend on generation). */
    assert(global_percent(0u)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Get_Config_Generation() == (uint16_t)(generation + 2u));
    assert(App_Gen_Get_Config_Crc() == crc_min);

    /* Not allowed while a configuration transaction is open. */
    assert(begin(1u, 0u)[2] == APP_CONTROL_STATUS_OK);
    assert(global_percent(5000u)[2] == APP_CONTROL_STATUS_INVALID_STATE);
    assert(simple(APP_CONTROL_OPCODE_ABORT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Get_Config_Generation() == (uint16_t)(generation + 2u));
}

static void test_global_percent_no_eligible_record(void)
{
    reset_fixture();
    assert(begin(1u, 0u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(J1939_KEY_EBS11_SPN5113, 1u)[2] == APP_CONTROL_STATUS_OK);  /* boolean */
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    const uint16_t generation = App_Gen_Get_Config_Generation();

    const uint8_t* r = global_percent(10000u);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert(r[3] == 1u);
    assert(read_u32(&r[4]) == generation);       /* affected 0, same generation */
    assert(App_Gen_Get_Config_Generation() == generation);
    assert(active_raw(J1939_KEY_EBS11_SPN5113) == 1u);
}

static void test_global_percent_while_running(void)
{
    reset_fixture();
    assert(begin(2u, 0u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(EEC1_SPEED_KEY, 0u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(EEC2_PEDAL_KEY, 0u)[2] == APP_CONTROL_STATUS_OK);
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    assert(simple(APP_CONTROL_OPCODE_START)[2] == APP_CONTROL_STATUS_OK);

    /* EEC1 broadcasts every 5000 ms (customer demo period). */
    for (s_now_ms = 1u; s_now_ms <= 5000u; s_now_ms++) {
        App_Gen_Process(s_now_ms);
    }
    const Test_Frame_t* eec1 = last_frame(EEC1_CAN_ID);
    const Test_Frame_t* eec2 = last_frame(EEC2_CAN_ID);
    assert(eec1 != NULL && eec2 != NULL);
    assert(eec1->data[3] == 0x00u && eec1->data[4] == 0x00u);
    assert(eec2->data[1] == 0x00u);

    /* 100 %: both signals jump to raw_max in the same generation. */
    const uint16_t tx_before = s_tx_count;
    const uint8_t* r = global_percent(10000u);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    assert((read_u32(&r[4]) >> 16) == 2u);
    assert(App_Gen_Is_Running());
    for (s_now_ms = 5001u; s_now_ms <= 10000u; s_now_ms++) {
        App_Gen_Process(s_now_ms);
    }
    for (uint16_t i = tx_before; i < s_tx_count; i++) {
        if (s_tx[i].can_id == EEC1_CAN_ID) {
            assert(s_tx[i].data[3] == 0xFFu && s_tx[i].data[4] == 0xFAu);  /* 64255 */
        }
        if (s_tx[i].can_id == EEC2_CAN_ID) {
            assert(s_tx[i].data[1] == 0xFAu);                              /* 250 */
        }
    }
    assert(last_frame(EEC1_CAN_ID) > eec1 && last_frame(EEC2_CAN_ID) > eec2);
    assert(simple(APP_CONTROL_OPCODE_STOP)[2] == APP_CONTROL_STATUS_OK);
}

static const uint8_t* set_string(uint16_t key, uint8_t total, uint8_t offset,
                                 const uint8_t* chars)
{
    uint8_t frame[8];
    make_blob(frame, key, total, offset, chars);
    frame[0] = APP_CONTROL_OPCODE_SET_STRING;
    return send_frame(frame);
}

static void test_set_string_vin(void)
{
    static const uint8_t vin[] = "1FUJGLDR5CLBP8834";
    const uint8_t vin_len = 17u;
    reset_fixture();

    /* Same bytes through WRITE_BLOB give the reference configuration CRC. */
    assert(begin(1u, 1u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(EEC1_SPEED_KEY, 12000u)[2] == APP_CONTROL_STATUS_OK);
    write_whole_blob(VIN_KEY, vin_len, vin);
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    const uint32_t blob_crc = App_Gen_Get_Config_Crc();

    reset_fixture();
    assert(begin(1u, 1u)[2] == APP_CONTROL_STATUS_OK);
    assert(set_raw(EEC1_SPEED_KEY, 12000u)[2] == APP_CONTROL_STATUS_OK);
    for (uint8_t offset = 0u; offset < vin_len; offset = (uint8_t)(offset + 3u)) {
        const uint8_t* r = set_string(VIN_KEY, vin_len, offset, vin);
        assert(r[0] == (APP_CONTROL_OPCODE_SET_STRING | APP_CONTROL_RESPONSE_MASK));
        assert(r[2] == APP_CONTROL_STATUS_OK && r[3] == vin_len);
        assert(read_u32(&r[4]) == (uint32_t)((offset + 3u < vin_len) ? offset + 3u : vin_len));
    }
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Get_Config_Crc() == blob_crc);
    const uint8_t* blob = NULL;
    uint16_t length = 0u;
    assert(J1939_Value_Store_Get_Blob(J1939_Catalog_Index_From_Key(VIN_KEY),
                                      &blob, &length));
    assert(length == vin_len && memcmp(blob, vin, vin_len) == 0);

    /* 20 chars is the longest string: the 21-byte VIN frame ends in '*'. */
    uint8_t text[200];
    for (uint16_t i = 0u; i < sizeof(text); i++) {
        text[i] = (uint8_t)('0' + (i % 10u));
    }
    assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
    for (uint8_t offset = 0u; offset < 20u; offset = (uint8_t)(offset + 3u)) {
        assert(set_string(VIN_KEY, 20u, offset, text)[2] == APP_CONTROL_STATUS_OK);
    }
    assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] == APP_CONTROL_STATUS_OK);
    assert(J1939_Value_Store_Get_Blob(J1939_Catalog_Index_From_Key(VIN_KEY),
                                      &blob, &length) && length == 20u);
    const uint16_t generation = App_Gen_Get_Config_Generation();
    const uint32_t crc = App_Gen_Get_Config_Crc();

    /* Every invalid chunk poisons its transaction; the active VIN stays. */
    struct {
        uint16_t key;
        uint8_t total;
        uint8_t offset;
        const char* chars;
        uint8_t status;
    } const bad[] = {
        { VIN_KEY,        17u,  0u,  "AB*",  APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { VIN_KEY,        17u,  3u,  "\x1F""AB", APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { VIN_KEY,        17u,  15u, "A\x7F", APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { VIN_KEY,        21u,  0u,  "ABC",  APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { VIN_KEY,        0u,   0u,  "",     APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { VIN_KEY,        17u,  17u, "",     APP_CONTROL_STATUS_VALUE_OUT_OF_RANGE },
        { EEC1_SPEED_KEY, 17u,  0u,  "ABC",  APP_CONTROL_STATUS_BAD_ARGUMENT },
        { 0u,             17u,  0u,  "ABC",  APP_CONTROL_STATUS_UNKNOWN_SIGNAL_KEY },
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        uint8_t chars[200] = { 0u };
        memcpy(&chars[bad[i].offset], bad[i].chars, strlen(bad[i].chars));
        assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
        assert(set_string(bad[i].key, bad[i].total, bad[i].offset, chars)[2] ==
               bad[i].status);
        assert(App_Gen_Config_Transaction_Rejected());
        assert(simple(APP_CONTROL_OPCODE_COMMIT_CONFIG)[2] ==
               APP_CONTROL_STATUS_TRANSACTION_REJECTED);
    }
    /* Non-zero padding after the last character is a malformed frame. */
    uint8_t frame[8];
    assert(begin(0u, 1u)[2] == APP_CONTROL_STATUS_OK);
    make_blob(frame, VIN_KEY, 17u, 15u, vin);
    frame[0] = APP_CONTROL_OPCODE_SET_STRING;
    frame[7] = 'X';
    assert(send_frame(frame)[2] == APP_CONTROL_STATUS_BAD_ARGUMENT);
    assert(simple(APP_CONTROL_OPCODE_ABORT_CONFIG)[2] == APP_CONTROL_STATUS_OK);

    assert(App_Gen_Get_Config_Generation() == generation);
    assert(App_Gen_Get_Config_Crc() == crc);
}

int main(void)
{
    test_protocol_version_and_limits_page();
    test_write_blob_vin_commit();
    test_write_blob_failures_keep_active();
    test_set_string_vin();
    test_global_percent_mapping();
    test_global_percent_full_profile();
    test_global_percent_no_eligible_record();
    test_global_percent_while_running();
    puts("Phase 4B WRITE_BLOB/SET_GLOBAL_PERCENT/config CRC host tests: PASS");
    return 0;
}
