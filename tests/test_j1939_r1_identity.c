#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal_console.h"
#include "j1939_link.h"
#include "j1939_pattern_generator.h"
#include "j1939_signal_definitions_legacy.h"
#include "j1939_tx_scheduler.h"

/* Link stubs: this host test exercises scheduler registration, not CAN TX. */
J1939_Link_Status_t J1939_Link_Send(uint32_t pgn,
                                    const uint8_t* data,
                                    uint8_t data_length,
                                    uint8_t priority,
                                    uint8_t source_address)
{
    (void)pgn;
    (void)data;
    (void)data_length;
    (void)priority;
    (void)source_address;
    return J1939_LINK_OK;
}

uint16_t hal_console_tx_free(void)
{
    return UINT16_MAX;
}

void hal_console_write_line(const char* str)
{
    (void)str;
}

static J1939_Signal_Id_t get_id(uint32_t pgn, uint32_t spn)
{
    const J1939_Signal_Definition_t* def =
        J1939_Find_Signal_By_PGN_SPN(pgn, spn);
    J1939_Signal_Id_t signal_id = J1939_SIGNAL_ID_INVALID;

    assert(def != NULL);
    assert(J1939_Get_Signal_Id(def, &signal_id));
    assert(J1939_Find_Signal_By_Id(signal_id) == def);
    return signal_id;
}

static void register_constant(J1939_Signal_Id_t signal_id, float value)
{
    const J1939_Signal_Definition_t* def = J1939_Find_Signal_By_Id(signal_id);
    assert(def != NULL);
    const uint16_t catalog_index =
        J1939_Catalog_Find_Signal_Index(def->pgn, def->spn);
    assert(catalog_index != J1939_CATALOG_INDEX_INVALID);
    Pattern_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.catalog_index = catalog_index;
    cfg.pattern_type = PATTERN_CONSTANT;
    cfg.min_value = 0.0f;
    cfg.max_value = 100.0f;
    cfg.initial_value = value;
    cfg.param1 = value;
    assert(Pattern_Generator_Register(&cfg));
}

static void test_mvp_ui_keys(void)
{
    const J1939_Signal_Definition_t* def;
    J1939_Signal_Id_t signal_id;
    J1939_Signal_Key_t signal_key = J1939_SIGNAL_KEY_INVALID;

    def = J1939_Find_Signal_By_Key(J1939_SIGNAL_KEY_ENGINE_SPEED);
    assert(def != NULL && def->pgn == PGN_EEC1 && def->spn == SPN_ENGINE_SPEED);
    assert(J1939_Get_Signal_Id(def, &signal_id));
    assert(J1939_Get_Signal_Key(signal_id, &signal_key));
    assert(signal_key == J1939_SIGNAL_KEY_ENGINE_SPEED);

    def = J1939_Find_Signal_By_Key(J1939_SIGNAL_KEY_ACCEL_PEDAL_POS);
    assert(def != NULL && def->pgn == PGN_EEC2 && def->spn == SPN_ACCEL_PEDAL_POS);

    def = J1939_Find_Signal_By_Key(J1939_SIGNAL_KEY_VEHICLE_SPEED);
    assert(def != NULL && def->pgn == PGN_CCVS1 && def->spn == SPN_VEHICLE_SPEED);

    assert(J1939_Find_Signal_By_Key(J1939_SIGNAL_KEY_INVALID) == NULL);
    assert(J1939_Find_Signal_By_Key(999u) == NULL);
}

static void test_duplicate_spns_are_independent(void)
{
    const J1939_Signal_Id_t id_928_eac2 = get_id(PGN_EAC2, 928u);
    const J1939_Signal_Id_t id_928_vw = get_id(PGN_VW, 928u);
    const J1939_Signal_Id_t id_1807_vdc2 = get_id(PGN_VDC2, 1807u);
    const J1939_Signal_Id_t id_1807_swa = get_id(PGN_SWA, 1807u);

    assert(id_928_eac2 != id_928_vw);
    assert(id_1807_vdc2 != id_1807_swa);

    Pattern_Generator_Init(0u);
    register_constant(id_928_eac2, 10.0f);
    register_constant(id_928_vw, 20.0f);
    register_constant(id_1807_vdc2, 30.0f);
    register_constant(id_1807_swa, 40.0f);
    assert(Pattern_Generator_Get_Count() == 4u);

    const J1939_Signal_Definition_t* d928e = J1939_Find_Signal_By_Id(id_928_eac2);
    const J1939_Signal_Definition_t* d928v = J1939_Find_Signal_By_Id(id_928_vw);
    const J1939_Signal_Definition_t* d1807v = J1939_Find_Signal_By_Id(id_1807_vdc2);
    const J1939_Signal_Definition_t* d1807s = J1939_Find_Signal_By_Id(id_1807_swa);
    const uint16_t c928e = J1939_Catalog_Find_Signal_Index(d928e->pgn, d928e->spn);
    const uint16_t c928v = J1939_Catalog_Find_Signal_Index(d928v->pgn, d928v->spn);
    const uint16_t c1807v = J1939_Catalog_Find_Signal_Index(d1807v->pgn, d1807v->spn);
    const uint16_t c1807s = J1939_Catalog_Find_Signal_Index(d1807s->pgn, d1807s->spn);

    assert(Pattern_Generator_Get_Value_Instant(c928e, 6000u, -1.0f) == 10.0f);
    assert(Pattern_Generator_Get_Value_Instant(c928v, 6000u, -1.0f) == 20.0f);
    assert(Pattern_Generator_Get_Value_Instant(c1807v, 6000u, -1.0f) == 30.0f);
    assert(Pattern_Generator_Get_Value_Instant(c1807s, 6000u, -1.0f) == 40.0f);

    register_constant(id_1807_vdc2, 55.0f);
    assert(Pattern_Generator_Get_Count() == 4u);
    assert(Pattern_Generator_Get_Value_Instant(c1807v, 6000u, -1.0f) == 55.0f);
    assert(Pattern_Generator_Get_Value_Instant(c1807s, 6000u, -1.0f) == 40.0f);

    J1939_Sched_Clear();
    assert(J1939_Sched_Add_Catalog_Signal(c928e, 0u, 0u));
    assert(J1939_Sched_Add_Catalog_Signal(c928v, 0u, 0u));
    assert(J1939_Sched_Add_Catalog_Signal(c1807v, 0u, 0u));
    assert(J1939_Sched_Add_Catalog_Signal(c1807s, 0u, 0u));
    assert(J1939_Sched_Get_Signal_Count() == 4u);
    /* VW is On-Request with the approved 1000 ms demo broadcast. */
    assert(J1939_Sched_Get_PGN_Count() == 4u);

    assert(J1939_Sched_Add_Catalog_Signal(c1807v, 25u, 0u));
    assert(J1939_Sched_Get_Signal_Count() == 4u);
}

int main(void)
{
    test_mvp_ui_keys();
    test_duplicate_spns_are_independent();
    puts("R1 signal identity host tests: PASS");
    return 0;
}
