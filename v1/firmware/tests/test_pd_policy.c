/* PD sink policy: the invariants from design.md section 6. */

#include "pd_policy.h"
#include "test.h"

static pd_sink_profile_t board_only(void) {
    pd_sink_profile_t p = {
        .need = PD_NEED_MINIMUM,
        .board_current_ma = 300,
        .loop_supply_current_ma = 0,
        .preferred_mv = 5000,
    };
    return p;
}

static pd_sink_profile_t with_loop_supply(void) {
    pd_sink_profile_t p = {
        .need = PD_NEED_LOOP_SUPPLY,
        .board_current_ma = 300,
        .loop_supply_current_ma = 100, /* 4 channels x 20 mA, boosted from 20 V */
        .preferred_mv = 20000,
    };
    return p;
}

/* A typical 60 W laptop charger. */
static const pd_source_pdo_t laptop_charger[] = {
    {PD_SUPPLY_FIXED, 5000, 3000, false},
    {PD_SUPPLY_FIXED, 9000, 3000, false},
    {PD_SUPPLY_FIXED, 15000, 3000, false},
    {PD_SUPPLY_FIXED, 20000, 3000, false},
};

/* A phone charger: 5 V and 9 V only. */
static const pd_source_pdo_t phone_charger[] = {
    {PD_SUPPLY_FIXED, 5000, 3000, false},
    {PD_SUPPLY_FIXED, 9000, 2000, false},
};

/* A dumb 5 V brick that still speaks PD. */
static const pd_source_pdo_t dumb_5v[] = {
    {PD_SUPPLY_FIXED, 5000, 1500, false},
};

TEST(epr_is_never_allowed) {
    /* The invariant, stated as a test so nobody "improves" it later. */
    CHECK(pd_policy_allows_epr() == false);
}

TEST(epr_pdos_are_refused_even_when_offered) {
    /* A 140 W EPR charger: the 28 V and 48 V PDOs are reachable only in EPR
     * mode, and the input stage is rated for 20 V.  Taking one would destroy
     * the board. */
    const pd_source_pdo_t epr_charger[] = {
        {PD_SUPPLY_FIXED, 5000, 3000, false},
        {PD_SUPPLY_FIXED, 20000, 5000, false},
        {PD_SUPPLY_FIXED, 28000, 5000, true},
        {PD_SUPPLY_FIXED, 48000, 5000, true},
    };
    pd_sink_profile_t p = with_loop_supply();
    pd_request_t r = pd_policy_select(epr_charger, 4, &p);

    CHECK_EQ(r.result, PD_RESULT_OK);
    CHECK_EQ(r.voltage_mv, 20000);
    CHECK_MSG(r.voltage_mv <= PD_SPR_MAX_MV, "never above the SPR ceiling");
}

TEST(a_source_lying_about_spr_is_still_refused) {
    /* Not flagged as EPR, but above the SPR ceiling: refuse it anyway.  The
     * check is on the voltage, not on the flag, because the flag comes from
     * the source. */
    const pd_source_pdo_t liar[] = {
        {PD_SUPPLY_FIXED, 5000, 3000, false},
        {PD_SUPPLY_FIXED, 28000, 5000, false},
    };
    pd_sink_profile_t p = with_loop_supply();
    pd_request_t r = pd_policy_select(liar, 2, &p);
    CHECK_EQ(r.voltage_mv, 5000);
    CHECK_EQ(r.result, PD_RESULT_FALLBACK_5V);
}

TEST(decoded_pdo_above_spr_is_marked_epr) {
    /* 48 V fixed: 960 * 50 mV, 5 A = 500 * 10 mA. */
    const uint32_t raw = (0u << 30) | (960u << 10) | 500u;
    pd_source_pdo_t pdo = pd_policy_decode_pdo(raw);
    CHECK_EQ(pdo.voltage_mv, 48000);
    CHECK_EQ(pdo.max_current_ma, 5000);
    CHECK(pdo.epr);
}

TEST(five_volts_is_always_acceptable) {
    /* The dev board must work off whatever is plugged in, including a brick
     * that offers nothing but 5 V. */
    pd_sink_profile_t p = with_loop_supply();
    pd_request_t r = pd_policy_select(dumb_5v, 1, &p);

    CHECK_EQ(r.result, PD_RESULT_FALLBACK_5V);
    CHECK_EQ(r.voltage_mv, 5000);
    CHECK_EQ(r.object_position, 1);
    CHECK_MSG(r.capability_mismatch, "tell the source we wanted more");
}

TEST(loop_supply_takes_the_highest_spr_voltage) {
    pd_sink_profile_t p = with_loop_supply();
    pd_request_t r = pd_policy_select(laptop_charger, 4, &p);
    CHECK_EQ(r.result, PD_RESULT_OK);
    CHECK_EQ(r.voltage_mv, 20000);
    CHECK_EQ(r.object_position, 4);
    CHECK(!r.capability_mismatch);
}

TEST(board_only_profile_is_happy_with_five_volts) {
    /* Section 5.2: the board's own consumption is under 2 W, so PD is for the
     * loop supply, not for the board.  With no loop supply needed, asking for
     * 20 V would be complexity for its own sake. */
    pd_sink_profile_t p = board_only();
    pd_request_t r = pd_policy_select(laptop_charger, 4, &p);
    CHECK_EQ(r.result, PD_RESULT_OK);
    CHECK_EQ(r.voltage_mv, 5000);
}

TEST(partial_charger_falls_back_with_mismatch_flag) {
    pd_sink_profile_t p = with_loop_supply();
    pd_request_t r = pd_policy_select(phone_charger, 2, &p);
    /* 9 V is the best on offer; take it, but say it was not enough. */
    CHECK_EQ(r.voltage_mv, 9000);
    CHECK_EQ(r.result, PD_RESULT_FALLBACK_5V);
    CHECK(r.capability_mismatch);
}

TEST(a_source_that_cannot_supply_the_current_is_skipped) {
    const pd_source_pdo_t weak_20v[] = {
        {PD_SUPPLY_FIXED, 5000, 3000, false},
        {PD_SUPPLY_FIXED, 20000, 100, false}, /* 100 mA: not enough */
    };
    pd_sink_profile_t p = with_loop_supply(); /* needs 400 mA */
    pd_request_t r = pd_policy_select(weak_20v, 2, &p);
    CHECK_EQ(r.voltage_mv, 5000);
}

TEST(non_fixed_supplies_are_declined) {
    const pd_source_pdo_t pps_only[] = {
        {PD_SUPPLY_FIXED, 5000, 3000, false},
        {PD_SUPPLY_APDO, 20000, 3000, false},
        {PD_SUPPLY_VARIABLE, 12000, 3000, false},
    };
    pd_sink_profile_t p = with_loop_supply();
    pd_request_t r = pd_policy_select(pps_only, 3, &p);
    CHECK_EQ(r.voltage_mv, 5000);
}

TEST(malformed_source_capabilities_are_rejected) {
    const pd_source_pdo_t bad_first[] = {
        {PD_SUPPLY_FIXED, 9000, 3000, false}, /* spec requires 5 V first */
    };
    pd_sink_profile_t p = board_only();
    pd_request_t r = pd_policy_select(bad_first, 1, &p);
    CHECK_EQ(r.result, PD_RESULT_INVALID_SOURCE);

    r = pd_policy_select(NULL, 0, &p);
    CHECK_EQ(r.result, PD_RESULT_INVALID_SOURCE);
}

TEST(bulk_capacitance_waits_for_the_contract) {
    /* Section 6.3: hanging 220 uF on VBUS before a contract makes sources
     * declare a fault, and the board then oscillates between negotiating and
     * browning out. */
    CHECK(pd_policy_bulk_enable_allowed(false, 10) == true);
    CHECK(pd_policy_bulk_enable_allowed(false, 11) == false);
    CHECK(pd_policy_bulk_enable_allowed(false, 220) == false);
    CHECK(pd_policy_bulk_enable_allowed(true, 220) == true);
}

TEST(rdo_encoding_matches_the_spec_layout) {
    pd_sink_profile_t p = with_loop_supply();
    pd_request_t r = pd_policy_select(laptop_charger, 4, &p);
    const uint32_t rdo = pd_policy_encode_rdo(&r, 3000, false);

    CHECK_EQ((rdo >> 28) & 0x7, 4);        /* object position */
    CHECK_EQ((rdo >> 10) & 0x3FF, 40);     /* 400 mA operating, in 10 mA units */
    CHECK_EQ(rdo & 0x3FF, 300);            /* 3000 mA max */
    CHECK_EQ((rdo >> 26) & 1, 0);          /* no capability mismatch */
    CHECK_EQ((rdo >> 25) & 1, 1);          /* USB comms capable */
}

TEST(pdo_decode_round_trips_a_fixed_supply) {
    /* 9 V at 3 A: 180 * 50 mV, 300 * 10 mA. */
    const uint32_t raw = (0u << 30) | (180u << 10) | 300u;
    pd_source_pdo_t pdo = pd_policy_decode_pdo(raw);
    CHECK_EQ(pdo.type, PD_SUPPLY_FIXED);
    CHECK_EQ(pdo.voltage_mv, 9000);
    CHECK_EQ(pdo.max_current_ma, 3000);
    CHECK(!pdo.epr);
}

TEST_MAIN("pd_policy", {
    RUN(epr_is_never_allowed);
    RUN(epr_pdos_are_refused_even_when_offered);
    RUN(a_source_lying_about_spr_is_still_refused);
    RUN(decoded_pdo_above_spr_is_marked_epr);
    RUN(five_volts_is_always_acceptable);
    RUN(loop_supply_takes_the_highest_spr_voltage);
    RUN(board_only_profile_is_happy_with_five_volts);
    RUN(partial_charger_falls_back_with_mismatch_flag);
    RUN(a_source_that_cannot_supply_the_current_is_skipped);
    RUN(non_fixed_supplies_are_declined);
    RUN(malformed_source_capabilities_are_rejected);
    RUN(bulk_capacitance_waits_for_the_contract);
    RUN(rdo_encoding_matches_the_spec_layout);
    RUN(pdo_decode_round_trips_a_fixed_supply);
})
