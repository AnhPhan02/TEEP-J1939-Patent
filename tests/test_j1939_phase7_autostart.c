/* Phase 7 standalone autostart: boot restore + CAN-late autostart.
 * A "power cycle" is OP_Init() with s_now = 0; the fake flash keeps its
 * content across cycles exactly like the two 4 KiB profile pages. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_boot.h"
#include "app_control_can.h"
#include "app_generator.h"
#include "app_persistence.h"
#include "hal_can.h"
#include "hal_console.h"
#include "hal_flash.h"
#include "hal_flash_fake.h"
#include "hal_led.h"
#include "hal_system.h"
#include "hal_time.h"
#include "j1939_customer_profiles.h"
#include "j1939_profile_storage.h"
#include "j1939_rx_router.h"
#include "j1939_signal_definitions.h"
#include "j1939_tp.h"
#include "j1939_value_store.h"
#include "op_config.h"
#include "op_main.h"

#define CONTROL_RX_ID  0x18EF80F9u
#define CONTROL_TX_ID  0x18EFF980u
/* Probe signal with a short period: CCVS1 SPN 84 (bytes 2-3, 100 ms). */
#define PGN_PROBE      0xFEF1u
#define SPN_PROBE      84u
#define KEY_PROBE      J1939_KEY_CCVS1_SPN84

static uint32_t s_now;
static bool     s_can_up = true;
static uint32_t s_can_init_bps;
static uint32_t s_can_init_calls;
static uint32_t s_self_test_bps;
static uint8_t  s_rx[8];
static bool     s_rx_pending;
static uint8_t  s_control_tx[8];
static uint32_t s_control_tx_count;
static uint32_t s_data_tx_count;
static uint8_t  s_probe[8];
static uint32_t s_probe_count;
static bool     s_bus_no_ack;           /* data frames stay in the mailboxes */
static hal_reset_cause_t s_reset_cause = HAL_RESET_POWER_ON;

/* BAM receiver: priority 7, TP.CM/TP.DT to global, source address 0. */
#define BAM_CM_ID_SA0  0x1CECFF00u
#define BAM_DT_ID_SA0  0x1CEBFF00u
typedef struct {
    uint8_t  data[32];
    uint32_t count;          /* complete messages */
    uint32_t cm_ms;          /* TP.CM time of the last message */
} Bam_Msg_t;
static struct {
    uint32_t pgn;
    uint16_t size;
    uint8_t  packets;
    uint8_t  received;
    uint8_t  data[32];
    uint32_t cm_ms;
    uint32_t last_ms;
} s_bam;
static Bam_Msg_t s_fee1;
static Bam_Msg_t s_fee3;
static Bam_Msg_t s_other_bam;
static uint32_t  s_bam_min_gap_ms = UINT32_MAX;
static uint32_t  s_bam_dt_frames;

static void bam_on_cm(const uint8_t* data)
{
    assert(data[0] == 32u && data[4] == 0xFFu);          /* BAM control byte */
    assert(s_bam.received == s_bam.packets);              /* previous one done */
    s_bam.size = (uint16_t)(data[1] | (data[2] << 8));
    s_bam.packets = data[3];
    s_bam.pgn = (uint32_t)data[5] | ((uint32_t)data[6] << 8) | ((uint32_t)data[7] << 16);
    assert(s_bam.size > 8u && s_bam.size <= sizeof(s_bam.data));
    assert(s_bam.packets == (s_bam.size + 6u) / 7u);
    s_bam.received = 0u;
    s_bam.cm_ms = s_now;
    s_bam.last_ms = s_now;
    memset(s_bam.data, 0, sizeof(s_bam.data));
}

static void bam_on_dt(const uint8_t* data)
{
    assert(s_bam.received < s_bam.packets);
    assert(data[0] == s_bam.received + 1u);               /* sequence 1..n */
    const uint32_t gap = s_now - s_bam.last_ms;
    if (gap < s_bam_min_gap_ms) s_bam_min_gap_ms = gap;
    s_bam.last_ms = s_now;
    memcpy(&s_bam.data[s_bam.received * 7u], &data[1], 7u);
    s_bam.received++;
    s_bam_dt_frames++;
    if (s_bam.received < s_bam.packets) {
        return;
    }
    for (uint16_t i = s_bam.size; i < s_bam.packets * 7u; i++) {
        assert(s_bam.data[i] == 0xFFu);                    /* final padding */
    }
    Bam_Msg_t* msg = (s_bam.pgn == 0xFEE1u) ? &s_fee1 :
                     (s_bam.pgn == 0xFEE3u) ? &s_fee3 : &s_other_bam;
    memcpy(msg->data, s_bam.data, s_bam.size);
    msg->count++;
    msg->cm_ms = s_bam.cm_ms;
}

/* ---- HAL stubs ---------------------------------------------------------- */
bool hal_can_init(uint32_t bitrate_bps)
{
    s_can_init_bps = bitrate_bps;
    s_can_init_calls++;
    return s_can_up;
}
bool hal_can_self_test(uint32_t bitrate_bps) { s_self_test_bps = bitrate_bps; return true; }
bool hal_can_receive_ext(uint32_t* can_id, uint8_t* data, uint8_t* length)
{
    if (!s_rx_pending) return false;
    *can_id = CONTROL_RX_ID;
    *length = 8u;
    memcpy(data, s_rx, 8u);
    s_rx_pending = false;
    return true;
}
hal_can_tx_status_t hal_can_send_ext(uint32_t can_id, const uint8_t* data,
                                     uint8_t length)
{
    assert(length <= 8u);
    if (can_id == CONTROL_TX_ID) {
        memcpy(s_control_tx, data, 8u);
        s_control_tx_count++;
        return HCAN_TX_OK;
    }
    if (s_bus_no_ack) {
        return HCAN_TX_TIMEOUT;
    }
    if ((can_id & 0xFFFFFF00u) == BAM_CM_ID_SA0) {
        bam_on_cm(data);
        return HCAN_TX_OK;
    }
    if ((can_id & 0xFFFFFF00u) == BAM_DT_ID_SA0) {
        bam_on_dt(data);
        return HCAN_TX_OK;
    }
    s_data_tx_count++;
    if (((can_id >> 8) & 0xFFFFu) == PGN_PROBE) {
        memcpy(s_probe, data, length);
        s_probe_count++;
    }
    return HCAN_TX_OK;
}
void hal_can_get_bit_timing(hal_can_bit_timing_t* out) { memset(out, 0, sizeof(*out)); }
void hal_can_get_error(hal_can_error_t* out)
{
    /* No ACK: TEC climbs to error passive (128) and stays there. */
    memset(out, 0, sizeof(*out));
    if (s_bus_no_ack) {
        out->tec = 128u;
        out->error_warning = true;
        out->error_passive = true;
    }
}
void hal_can_diagnose(hal_can_diag_t* out) { memset(out, 0, sizeof(*out)); }
bool hal_system_init(void) { return true; }
void hal_system_get_clocks(hal_clock_info_t* out) { memset(out, 0, sizeof(*out)); }
hal_reset_cause_t hal_system_get_reset_cause(void) { return s_reset_cause; }
const char* hal_system_reset_cause_name(hal_reset_cause_t cause) { (void)cause; return "POR"; }
void hal_watchdog_start(uint32_t timeout_ms) { (void)timeout_ms; }
void hal_watchdog_feed(void) {}
void hal_time_init(uint32_t hclk_hz) { (void)hclk_hz; }
uint32_t hal_time_ms(void) { return s_now; }
void hal_led_init(void) {}
void hal_led_set(bool on) { (void)on; }
void hal_console_init(uint32_t baud) { (void)baud; }
void hal_console_write(const char* value) { (void)value; }
void hal_console_write_line(const char* value) { (void)value; }
void hal_console_write_u32(uint32_t value) { (void)value; }
void hal_console_write_i32(int32_t value) { (void)value; }
void hal_console_write_hex(uint32_t value) { (void)value; }
void hal_console_write_float(float value, uint8_t decimals)
{ (void)value; (void)decimals; }
uint16_t hal_console_tx_free(void) { return UINT16_MAX; }

/* ---- helpers ------------------------------------------------------------ */
static void power_cycle(bool can_up)
{
    s_now = 0u;
    s_can_up = can_up;
    s_can_init_bps = 0u;
    s_can_init_calls = 0u;
    s_self_test_bps = 0u;
    s_rx_pending = false;
    s_control_tx_count = 0u;
    s_data_tx_count = 0u;
    s_probe_count = 0u;
    OP_Init();
}

static void run_until(uint32_t end_ms)
{
    while (s_now < end_ms) {
        s_now++;
        OP_Step(s_now);
    }
}

static const uint8_t* send_control(const uint8_t frame[8])
{
    const uint32_t before = s_control_tx_count;
    memcpy(s_rx, frame, 8u);
    s_rx_pending = true;
    J1939_Rx_Router_Poll(1u);
    App_Control_Can_Process();
    assert(s_control_tx_count == before + 1u);
    return s_control_tx;
}

static App_Boot_Info_t boot_info(void)
{
    App_Boot_Info_t info;
    App_Boot_Get_Info(&info);
    return info;
}

static bool is_startable_scalar(const J1939_Catalog_Signal_t* signal)
{
    const J1939_Catalog_PGN_t* pgn = &J1939_Catalog_PGNs[signal->pgn_index];
    return ((signal->flags & J1939_CAT_SIG_BLOB) == 0u) &&
           (pgn->payload_length <= 8u) &&
           ((pgn->flags & J1939_CAT_PGN_REQUIRES_TP) == 0u);
}

/* Profile D carries no blob, so the same path builds TP and non-TP profiles. */
static void configure_profile(uint8_t profile_id)
{
    const J1939_Customer_Profile_t* profile =
        J1939_Customer_Profile_Find(profile_id);
    uint16_t scalars = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[i];
        if (J1939_Customer_Profile_Has_Key(profile, signal->signal_key)) {
            assert((signal->flags & J1939_CAT_SIG_BLOB) == 0u);
            scalars++;
        }
    }
    assert(App_Gen_Begin_Profile_Config(scalars, 0u, profile_id) ==
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

static void configure_engine_speed(uint32_t raw)
{
    assert(App_Gen_Begin_Config(1u, 0u) == APP_CONFIG_TXN_OK);
    assert(App_Gen_Stage_Constant_Raw(KEY_PROBE, raw, NULL) ==
           APP_CONFIG_TXN_OK);
    assert(App_Gen_Commit_Config(NULL, NULL, NULL) == APP_CONFIG_TXN_OK);
}

static uint32_t save(bool autostart)
{
    Profile_Storage_Info_t info;
    assert(App_Persistence_Save(&info) == APP_PERSISTENCE_OK);
    assert(App_Persistence_Set_Autostart(autostart, &info) ==
           APP_PERSISTENCE_OK);
    return info.config_crc;
}

/* ---- tests -------------------------------------------------------------- */
static void test_customer_c_autostart_after_power_cycle(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    assert(boot_info().result == APP_BOOT_EMPTY);
    assert(!App_Gen_Is_Running());

    configure_profile(J1939_PROFILE_ID_C);
    const uint32_t crc = save(true);

    power_cycle(true);
    const App_Boot_Info_t info = boot_info();
    assert(info.result == APP_BOOT_LOADED);
    assert(info.autostart == APP_AUTOSTART_STARTED);
    assert(info.config_crc == crc && App_Gen_Get_Config_Crc() == crc);
    assert(App_Gen_Get_Active_Profile_Id() == J1939_PROFILE_ID_C);
    assert(OP_Get_State() == OP_STATE_RUN && App_Gen_Is_Running());
    run_until(1000u);
    assert(s_data_tx_count > 0u);
}

static void test_payload_and_saved_bitrate(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    assert(App_Gen_Set_Baud(500u));
    configure_engine_speed(0x1234u);
    (void)save(true);

    power_cycle(true);
    assert(s_can_init_bps == 500000u && s_self_test_bps == 500000u);
    assert(App_Gen_Get_Baud() == 500u);
    assert(App_Gen_Is_Running());
    run_until(200u);
    assert(s_probe_count > 0u);
    /* SPN 84 occupies bytes 2-3 (1-based), little endian. */
    assert(s_probe[1] == 0x34u && s_probe[2] == 0x12u);
}

static void test_autostart_off_and_empty(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    configure_engine_speed(100u);
    const uint32_t crc = save(false);

    power_cycle(true);
    assert(boot_info().result == APP_BOOT_LOADED);
    assert(boot_info().autostart == APP_AUTOSTART_OFF);
    assert(App_Gen_Get_Config_Crc() == crc);
    assert(!App_Gen_Is_Running());
    run_until(500u);
    assert(s_data_tx_count == 0u);

    /* START from the UI still works on the restored profile. */
    const uint8_t start[8] = { APP_CONTROL_OPCODE_START, 1u };
    assert(send_control(start)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Is_Running());

    Test_Flash_Reset();
    power_cycle(true);
    assert(boot_info().result == APP_BOOT_EMPTY);
    assert(!App_Gen_Has_Configured_Signals() && !App_Gen_Is_Running());
}

static void test_corrupt_slots(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    configure_engine_speed(200u);
    const uint32_t crc1 = save(true);           /* seq 1 -> A, seq 2 -> B */
    App_Gen_Stop();
    configure_engine_speed(300u);
    const uint32_t crc2 = save(true);           /* seq 3 -> A, seq 4 -> B */
    assert(crc1 != crc2);

    Profile_Storage_Info_t latest;
    assert(Profile_Storage_Open_Latest(&latest) == PROFILE_STORAGE_OK);
    const uint32_t newest = (latest.slot == 0u)
        ? HAL_FLASH_PROFILE_SLOT_A_ADDRESS : HAL_FLASH_PROFILE_SLOT_B_ADDRESS;
    const uint32_t older = (latest.slot == 0u)
        ? HAL_FLASH_PROFILE_SLOT_B_ADDRESS : HAL_FLASH_PROFILE_SLOT_A_ADDRESS;

    /* The older slot holds the same config (SET_AUTOSTART rewrote it). */
    Test_Flash_Corrupt(newest + 40u, 0u);
    power_cycle(true);
    assert(boot_info().result == APP_BOOT_LOADED);
    assert(App_Gen_Get_Config_Crc() == crc2 && App_Gen_Is_Running());

    Test_Flash_Corrupt(older + 40u, 0u);
    uint8_t before[TEST_FLASH_BYTES];
    uint8_t after[TEST_FLASH_BYTES];
    Test_Flash_Snapshot(before);
    power_cycle(true);
    assert(boot_info().result == APP_BOOT_EMPTY ||
           boot_info().result == APP_BOOT_LOAD_FAILED);
    assert(!App_Gen_Has_Configured_Signals() && !App_Gen_Is_Running());
    run_until(500u);
    assert(s_data_tx_count == 0u);
    Test_Flash_Snapshot(after);
    assert(memcmp(before, after, sizeof(before)) == 0);   /* never erased */
}

static void test_unsupported_bitrate_is_rejected(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    assert(App_Gen_Set_Baud(125u));
    configure_engine_speed(400u);
    (void)save(true);

    power_cycle(true);
    const App_Boot_Info_t info = boot_info();
    assert(info.result == APP_BOOT_LOAD_FAILED);
    assert(info.load_status == APP_PERSISTENCE_UNSUPPORTED_CONFIG);
    assert(s_can_init_bps == 250000u);           /* default, not the image */
    assert(!App_Gen_Has_Configured_Signals() && !App_Gen_Is_Running());

    /* LOAD_FAILED never leaves a profile in RAM, whatever was there. */
    configure_engine_speed(401u);
    App_Boot_Restore(false, false);
    assert(boot_info().result == APP_BOOT_LOAD_FAILED);
    assert(!App_Gen_Has_Configured_Signals());
}

static void test_can_late_then_autostart(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    configure_engine_speed(500u);
    const uint32_t crc = save(true);

    power_cycle(false);
    assert(OP_Get_State() == OP_STATE_COMM_FAULT);
    assert(boot_info().autostart == APP_AUTOSTART_PENDING);
    assert(App_Gen_Get_Config_Crc() == crc);      /* profile kept in RAM */
    assert(!App_Gen_Is_Running());
    run_until(2999u);
    assert(OP_Get_State() == OP_STATE_COMM_FAULT && s_data_tx_count == 0u);
    assert(s_can_init_calls == 3u);               /* boot + 2 s of retries */

    s_can_up = true;
    run_until(4000u);
    assert(OP_Get_State() == OP_STATE_RUN);
    assert(boot_info().autostart == APP_AUTOSTART_STARTED);
    assert(App_Gen_Is_Running() && App_Gen_Get_Config_Crc() == crc);
    assert(s_data_tx_count > 0u);
}

static void test_operator_stop_cancels_pending_autostart(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    configure_engine_speed(600u);
    (void)save(true);

    /* Control STOP (the router is polled directly: COMM_FAULT has no RX). */
    power_cycle(false);
    const uint8_t stop[8] = { APP_CONTROL_OPCODE_STOP, 2u };
    assert(send_control(stop)[2] == APP_CONTROL_STATUS_OK);
    assert(boot_info().autostart == APP_AUTOSTART_CANCELLED);
    s_can_up = true;
    run_until(3000u);
    assert(OP_Get_State() == OP_STATE_RUN && !App_Gen_Is_Running());

    /* CLI STOP path. */
    power_cycle(false);
    App_Boot_Cancel_Autostart();
    App_Gen_Stop();
    s_can_up = true;
    run_until(3000u);
    assert(OP_Get_State() == OP_STATE_RUN && !App_Gen_Is_Running());
    assert(App_Gen_Has_Configured_Signals());
}

static void bam_reset(void)
{
    memset(&s_bam, 0, sizeof(s_bam));
    memset(&s_fee1, 0, sizeof(s_fee1));
    memset(&s_fee3, 0, sizeof(s_fee3));
    memset(&s_other_bam, 0, sizeof(s_other_bam));
    s_bam_min_gap_ms = UINT32_MAX;
    s_bam_dt_frames = 0u;
}

/* Customer D (FEE1/FEE3 multi-packet records) autostarts and broadcasts. */
static void test_tp_profile_autostarts_with_bam(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    configure_profile(J1939_PROFILE_ID_D);
    const uint32_t crc = save(true);

    bam_reset();
    power_cycle(true);
    const App_Boot_Info_t info = boot_info();
    assert(info.result == APP_BOOT_LOADED);
    assert(info.autostart == APP_AUTOSTART_STARTED);
    assert(App_Gen_Get_Config_Crc() == crc && App_Gen_Is_Running());
    run_until(5300u);
    assert(s_fee1.count >= 1u && s_fee3.count >= 1u);
    assert(s_bam_min_gap_ms >= J1939_TP_BAM_PACKET_GAP_MS);
}

/* FEE1/FEE3 payload, 5 s fallback period, deferral, no-ACK abort, STOP. */
static void test_bam_payload_timing_and_aborts(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    assert(App_Gen_Begin_Config(3u, 0u) == APP_CONFIG_TXN_OK);
    assert(App_Gen_Stage_Constant_Raw(J1939_KEY_RC_SPN901, 5u, NULL) ==
           APP_CONFIG_TXN_OK);                                /* 4 bits @ bit 0 */
    assert(App_Gen_Stage_Constant_Raw(J1939_KEY_RC_SPN556, 0x1234u, NULL) ==
           APP_CONFIG_TXN_OK);                                /* bytes 17-18 */
    assert(App_Gen_Stage_Constant_Raw(J1939_KEY_EC_SPN544, 0xABCDu, NULL) ==
           APP_CONFIG_TXN_OK);                                /* bytes 20-21 */
    assert(App_Gen_Commit_Config(NULL, NULL, NULL) == APP_CONFIG_TXN_OK);
    (void)save(true);

    bam_reset();
    power_cycle(true);
    assert(App_Gen_Is_Running());
    /* Only FEE1/FEE3 share the 5 s period: phases 2.5 s and 5 s. Just before
     * the second one is due, occupy the single TP session with another BAM:
     * the due PGN must wait and follow it, not be lost. */
    run_until(4990u);
    assert(s_fee1.count + s_fee3.count == 1u);
    uint8_t* other = J1939_Tp_Bam_Prepare(18u);
    assert(other != NULL);
    memset(other, 0x11, 18u);
    assert(J1939_Tp_Bam_Start(0xFEECu, 0u, s_now));
    run_until(5400u);
    assert(s_other_bam.count == 1u);
    assert(s_fee1.count == 1u && s_fee3.count == 1u);
    const Bam_Msg_t* second = (s_fee1.cm_ms > s_fee3.cm_ms) ? &s_fee1 : &s_fee3;
    assert(second->cm_ms > 5000u);                         /* deferred... */
    assert(second->cm_ms <= 5000u + 4u * J1939_TP_BAM_PACKET_GAP_MS); /* ...briefly */

    assert(s_fee1.data[0] == 0xF5u);                       /* SPN 901 = 5 */
    for (uint8_t i = 1u; i < 16u; i++) assert(s_fee1.data[i] == 0xFFu);
    assert(s_fee1.data[16] == 0x34u && s_fee1.data[17] == 0x12u);
    for (uint8_t i = 0u; i < 19u; i++) assert(s_fee3.data[i] == 0xFFu);
    assert(s_fee3.data[19] == 0xCDu && s_fee3.data[20] == 0xABu);
    assert(s_bam_min_gap_ms >= J1939_TP_BAM_PACKET_GAP_MS);

    run_until(10400u);                                     /* 5 s fallback */
    assert(s_fee1.count == 2u && s_fee3.count == 2u);

    /* Bus loses ACK in the middle of a BAM: the session is abandoned after
     * the receiver timeout and the next period starts a clean message. */
    run_until(12510u);                                     /* FEE1 CM at 12.5 s */
    J1939_Tp_Stats_t tp;
    J1939_Tp_Get_Stats(&tp);
    const uint32_t aborted = tp.aborted;
    assert(J1939_Tp_Is_Busy());
    s_bus_no_ack = true;
    run_until(13400u);
    J1939_Tp_Get_Stats(&tp);
    assert(tp.aborted == aborted + 1u && !J1939_Tp_Is_Busy());
    s_bus_no_ack = false;
    s_bam.received = s_bam.packets;                        /* receiver times out */
    run_until(17700u);
    assert(s_fee1.count == 3u);                            /* next period OK */

    /* STOP during a BAM cancels it: no TP.DT after STOP. */
    run_until(20010u);
    assert(J1939_Tp_Is_Busy());
    App_Gen_Stop();
    assert(!J1939_Tp_Is_Busy());
    const uint32_t dt_after_stop = s_bam_dt_frames;
    run_until(21000u);
    assert(s_bam_dt_frames == dt_after_stop);
    s_bam.received = s_bam.packets;
}

static void test_safe_mode_and_defaults(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    configure_engine_speed(700u);
    const uint32_t crc = save(true);

    App_Gen_Init(250u);
    App_Persistence_Init();
    App_Boot_Restore(true, false);
    assert(boot_info().result == APP_BOOT_LOADED);
    assert(boot_info().autostart == APP_AUTOSTART_SUPPRESSED);
    assert(App_Gen_Get_Config_Crc() == crc);
    assert(!App_Boot_Try_Autostart() && !App_Gen_Is_Running());

    /* A saved profile wins over the demo defaults... */
    App_Gen_Init(250u);
    App_Boot_Restore(false, true);
    assert(boot_info().result == APP_BOOT_LOADED);

    /* ...which are used only for an empty store. */
    Test_Flash_Reset();
    App_Gen_Init(250u);
    App_Persistence_Init();
    App_Boot_Restore(false, true);
    assert(boot_info().result == APP_BOOT_DEFAULTS);
    assert(boot_info().autostart == APP_AUTOSTART_PENDING);
    assert(App_Boot_Try_Autostart() && App_Gen_Is_Running());
    assert(!App_Boot_Try_Autostart());            /* fires once */
    App_Gen_Stop();
}

static uint32_t s_rng = 0x1939C0DEu;
static uint32_t rng(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

static void test_hundred_power_cycles(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    uint32_t expected_seq = 0u;
    for (uint32_t cycle = 0u; cycle < 100u; cycle++) {
        App_Gen_Stop();
        const uint32_t kbps = (cycle & 1u) ? 500u : 250u;
        assert(App_Gen_Set_Baud(kbps));

        /* The periodic probe signal proves the first frame each cycle; the
         * rest is a random subset (0..31) of startable scalars, which may be
         * ON_CHANGE/ON_REQUEST only, with random raw values. */
        uint16_t picks[32];
        uint16_t count = (uint16_t)(1u + (rng() % 32u));
        picks[0] = J1939_Catalog_Find_Signal_Index(PGN_PROBE, SPN_PROBE);
        uint16_t taken = 1u;
        while (taken < count) {
            const uint16_t index = (uint16_t)(rng() % J1939_CATALOG_SIGNAL_COUNT);
            const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[index];
            bool dup = false;
            for (uint16_t k = 0u; k < taken; k++) dup = dup || (picks[k] == index);
            if (!dup && is_startable_scalar(signal)) picks[taken++] = index;
        }
        assert(App_Gen_Begin_Config(count, 0u) == APP_CONFIG_TXN_OK);
        for (uint16_t k = 0u; k < count; k++) {
            const J1939_Catalog_Signal_t* signal = &J1939_Catalog_Signals[picks[k]];
            const uint32_t span = signal->raw_max - signal->raw_min;
            const uint32_t raw = signal->raw_min +
                ((span == UINT32_MAX) ? rng() : (rng() % (span + 1u)));
            assert(App_Gen_Stage_Constant_Raw(signal->signal_key, raw, NULL) ==
                   APP_CONFIG_TXN_OK);
        }
        assert(App_Gen_Commit_Config(NULL, NULL, NULL) == APP_CONFIG_TXN_OK);
        const uint32_t crc = App_Gen_Get_Config_Crc();

        Profile_Storage_Info_t saved;
        assert(App_Persistence_Save(&saved) == APP_PERSISTENCE_OK);
        expected_seq++;
        if (cycle == 0u) {
            assert(App_Persistence_Set_Autostart(true, &saved) ==
                   APP_PERSISTENCE_OK);
            expected_seq++;
        }
        assert(saved.sequence == expected_seq);

        /* A third of the cycles boot with the CAN bus unavailable. */
        const bool can_late = (cycle % 3u) == 2u;
        power_cycle(!can_late);
        if (can_late) {
            assert(OP_Get_State() == OP_STATE_COMM_FAULT && !App_Gen_Is_Running());
            s_can_up = true;
            run_until(OP_CAN_RETRY_PERIOD_MS + 1u);
        }
        const App_Boot_Info_t info = boot_info();
        assert(info.result == APP_BOOT_LOADED && info.sequence == expected_seq);
        assert(info.autostart == APP_AUTOSTART_STARTED);
        assert(App_Gen_Get_Config_Crc() == crc && info.config_crc == crc);
        assert(s_can_init_bps == kbps * 1000u);
        assert(OP_Get_State() == OP_STATE_RUN && App_Gen_Is_Running());
        run_until(s_now + 300u);
        assert(s_data_tx_count > 0u);
    }
}

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint8_t* get_status(uint8_t page)
{
    static uint8_t seq = 0x40u;
    const uint8_t frame[8] = { APP_CONTROL_OPCODE_GET_STATUS, ++seq, page };
    const uint8_t* r = send_control(frame);
    assert(r[0] == (APP_CONTROL_OPCODE_GET_STATUS | APP_CONTROL_RESPONSE_MASK));
    assert(r[2] == APP_CONTROL_STATUS_OK);
    return r;
}

/* Step 5: unplugged/no-ACK bus while running, then reconnected. */
static void test_no_ack_outage_recovers_without_burst(void)
{
    Test_Flash_Reset();
    power_cycle(true);
    configure_engine_speed(0x1234u);
    (void)save(true);
    power_cycle(true);
    assert(App_Gen_Is_Running());
    run_until(500u);
    const uint32_t before_outage = s_data_tx_count;
    assert(before_outage > 0u);

    s_bus_no_ack = true;
    run_until(2500u);
    assert(s_data_tx_count == before_outage);
    J1939_Sched_Stats_t stats;
    J1939_Sched_Get_Stats(&stats);
    assert(stats.expired > 0u);                   /* stale frames discarded */
    assert(stats.queued <= 1u);                   /* backlog never builds up */
    const uint8_t* link = get_status(APP_CONTROL_STATUS_PAGE_LINK);
    assert((link[3] & APP_CONTROL_BUS_ERROR_PASSIVE) != 0u);
    assert(link[6] == 128u);                      /* TEC */
    assert(App_Gen_Get_Bus_Recovery_Count() == 0u);

    s_bus_no_ack = false;
    const uint32_t at_recovery = s_data_tx_count;
    run_until(s_now + 2u);
    assert(s_data_tx_count - at_recovery <= 1u);  /* no stale burst */
    /* Phase was kept through the outage: steady rate right after recovery. */
    J1939_Sched_Get_Stats(&stats);
    const uint32_t missed_at_recovery = stats.deadline_missed;
    run_until(s_now + 200u);                      /* > one supervision period */
    J1939_Sched_Get_Stats(&stats);
    assert(stats.deadline_missed == missed_at_recovery);
    assert(App_Gen_Get_Bus_Recovery_Count() == 1u);
    assert(App_Gen_Is_Running() && s_probe[1] == 0x34u && s_probe[2] == 0x12u);
    link = get_status(APP_CONTROL_STATUS_PAGE_LINK);
    assert(link[3] == 0u && read_u32(&link[4]) == 1u);
}

/* Step 6/7: consecutive IWDG resets -> safe mode, reported on page 8. */
static void test_watchdog_boot_loop_guard(void)
{
    Test_Flash_Reset();
    s_reset_cause = HAL_RESET_POWER_ON;
    power_cycle(true);
    configure_engine_speed(800u);
    const uint32_t crc = save(true);

    s_reset_cause = HAL_RESET_IWDG;
    for (uint8_t streak = 1u; streak < OP_WDG_SAFE_MODE_STREAK; streak++) {
        power_cycle(true);                        /* watchdog reset resumes */
        assert(boot_info().watchdog_streak == streak);
        assert(boot_info().autostart == APP_AUTOSTART_STARTED);
        assert(App_Gen_Is_Running());
    }
    power_cycle(true);
    App_Boot_Info_t info = boot_info();
    assert(info.watchdog_streak == OP_WDG_SAFE_MODE_STREAK);
    assert(info.result == APP_BOOT_LOADED);
    assert(info.autostart == APP_AUTOSTART_SUPPRESSED);
    assert(!App_Gen_Is_Running() && App_Gen_Get_Config_Crc() == crc);

    const uint8_t* boot = get_status(APP_CONTROL_STATUS_PAGE_BOOT);
    assert(boot[3] == (uint8_t)HAL_RESET_IWDG);
    assert(boot[4] == (uint8_t)APP_BOOT_LOADED);
    assert(boot[5] == (uint8_t)APP_PERSISTENCE_OK);
    assert(boot[6] == (uint8_t)APP_AUTOSTART_SUPPRESSED);
    assert(boot[7] == OP_WDG_SAFE_MODE_STREAK);

    const uint8_t start[8] = { APP_CONTROL_OPCODE_START, 0x50u };
    assert(send_control(start)[2] == APP_CONTROL_STATUS_OK);
    assert(App_Gen_Is_Running());                 /* operator may override */

    /* Any non-watchdog reset clears the streak. */
    s_reset_cause = HAL_RESET_PIN;
    power_cycle(true);
    assert(boot_info().watchdog_streak == 0u);
    assert(boot_info().autostart == APP_AUTOSTART_STARTED);

    /* Running OP_BOOT_STABLE_MS without a reset also clears it. */
    s_reset_cause = HAL_RESET_IWDG;
    power_cycle(true);
    power_cycle(true);
    assert(boot_info().watchdog_streak == 2u);
    run_until(OP_BOOT_STABLE_MS);
    power_cycle(true);
    assert(boot_info().watchdog_streak == 1u);
    assert(App_Gen_Is_Running());

    s_reset_cause = HAL_RESET_POWER_ON;
    power_cycle(true);
    boot = get_status(APP_CONTROL_STATUS_PAGE_BOOT);
    assert(boot[3] == (uint8_t)HAL_RESET_POWER_ON && boot[7] == 0u);
    assert(boot[6] == (uint8_t)APP_AUTOSTART_STARTED);
}

/* VIN entered with SET_STRING inside a transaction is saved and restored. */
static void test_set_string_vin_survives_power_cycle(void)
{
    static const char vin[] = "1FUJGLDR5CLBP8834";
    const uint8_t vin_len = 17u;
    uint8_t seq = 0x60u;
    Test_Flash_Reset();
    power_cycle(true);

    uint8_t begin[8] = { APP_CONTROL_OPCODE_BEGIN_CONFIG, ++seq };
    for (uint8_t i = 0u; i < 4u; i++) {
        begin[2u + i] = (uint8_t)(J1939_CATALOG_CRC32 >> (8u * i));
    }
    begin[6] = 1u;                                /* one scalar */
    begin[7] = 1u;                                /* one blob */
    assert(send_control(begin)[2] == APP_CONTROL_STATUS_OK);
    const uint8_t set[8] = {
        APP_CONTROL_OPCODE_SET_SIGNAL_RAW, ++seq,
        (uint8_t)KEY_PROBE, (uint8_t)(KEY_PROBE >> 8),
        0x34u, 0x12u, 0u, 0u
    };
    assert(send_control(set)[2] == APP_CONTROL_STATUS_OK);
    for (uint8_t offset = 0u; offset < vin_len; offset = (uint8_t)(offset + 3u)) {
        uint8_t chunk[8] = { APP_CONTROL_OPCODE_SET_STRING, ++seq,
                             (uint8_t)J1939_KEY_VI_SPN237, vin_len, offset };
        for (uint8_t i = 0u; (i < 3u) && (offset + i < vin_len); i++) {
            chunk[5u + i] = (uint8_t)vin[offset + i];
        }
        assert(send_control(chunk)[2] == APP_CONTROL_STATUS_OK);
    }
    const uint8_t commit[8] = { APP_CONTROL_OPCODE_COMMIT_CONFIG, ++seq };
    assert(send_control(commit)[2] == APP_CONTROL_STATUS_OK);
    const uint8_t save_frame[8] = { APP_CONTROL_OPCODE_SAVE_CONFIG, ++seq };
    const uint8_t* r = send_control(save_frame);
    assert(r[2] == APP_CONTROL_STATUS_OK);
    const uint32_t crc = read_u32(&r[4]);
    assert(crc == App_Gen_Get_Config_Crc());

    power_cycle(true);
    assert(boot_info().result == APP_BOOT_LOADED && boot_info().config_crc == crc);
    assert(App_Gen_Get_Config_Crc() == crc);
    const uint8_t* blob = NULL;
    uint16_t length = 0u;
    assert(J1939_Value_Store_Get_Blob(
        J1939_Catalog_Index_From_Key(J1939_KEY_VI_SPN237), &blob, &length));
    assert(length == vin_len && memcmp(blob, vin, vin_len) == 0);

    /* FEEC goes out like FEE3: a fixed 21-byte BAM every 5 s,
     * VIN text + '*' + 0xFF padding. */
    const uint8_t start[8] = { APP_CONTROL_OPCODE_START, ++seq };
    assert(send_control(start)[2] == APP_CONTROL_STATUS_OK);
    bam_reset();
    run_until(5300u);
    assert(s_other_bam.count == 1u);
    static const uint8_t expected[21] = {
        '1', 'F', 'U', 'J', 'G', 'L', 'D', 'R', '5', 'C', 'L', 'B', 'P', '8',
        '8', '3', '4', '*', 0xFFu, 0xFFu, 0xFFu
    };
    assert(memcmp(s_other_bam.data, expected, sizeof(expected)) == 0);
    assert(s_bam.size == J1939_VIN_TX_BYTES && s_bam.pgn == 0xFEECu);

    /* A longer blob (WRITE_BLOB allows up to 200 B) is cut to 20 chars + '*'. */
    static const char long_vin[] = "ABCDEFGHIJKLMNOPQRSTUVWXY";      /* 25 chars */
    App_Gen_Stop();
    assert(App_Gen_Begin_Config(0u, 1u) == APP_CONFIG_TXN_OK);
    for (uint8_t offset = 0u; offset < 25u; offset = (uint8_t)(offset + 3u)) {
        uint8_t chunk[3] = { 0u, 0u, 0u };
        for (uint8_t i = 0u; (i < 3u) && (offset + i < 25u); i++) {
            chunk[i] = (uint8_t)long_vin[offset + i];
        }
        assert(App_Gen_Stage_Blob_Chunk(J1939_KEY_VI_SPN237, 25u, offset, chunk,
                                        NULL) == APP_CONFIG_TXN_OK);
    }
    assert(App_Gen_Commit_Config(NULL, NULL, NULL) == APP_CONFIG_TXN_OK);
    App_Gen_Start(0u, J1939_SCHED_MODE_SAE);
    bam_reset();
    run_until(s_now + 5300u);
    assert(s_other_bam.count == 1u);
    assert(memcmp(s_other_bam.data, long_vin, 20u) == 0);
    assert(s_other_bam.data[20] == '*');
    App_Gen_Stop();
}

int main(void)
{
    test_no_ack_outage_recovers_without_burst();
    test_watchdog_boot_loop_guard();
    test_set_string_vin_survives_power_cycle();
    test_customer_c_autostart_after_power_cycle();
    test_payload_and_saved_bitrate();
    test_autostart_off_and_empty();
    test_corrupt_slots();
    test_unsupported_bitrate_is_rejected();
    test_can_late_then_autostart();
    test_operator_stop_cancels_pending_autostart();
    test_tp_profile_autostarts_with_bam();
    test_bam_payload_timing_and_aborts();
    test_safe_mode_and_defaults();
    test_hundred_power_cycles();
    puts("Phase 7 standalone autostart tests: PASS (100/100 power cycles)");
    return 0;
}
