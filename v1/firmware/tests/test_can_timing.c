/* CAN bit timing, and the crystal-versus-HSI question from design.md 7.2. */

#include "can_timing.h"
#include "test.h"

#define FDCAN_CLK_HZ 170000000u

TEST(classic_500k_is_exact_at_170mhz) {
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 500000, 875, false);
    CHECK(t.valid);
    CHECK_EQ(t.bitrate_bps, 500000);
    CHECK_EQ(t.bitrate_error_ppm, 0);
    /* Within the solver's documented tie band: it trades a percent of sample
     * point for a bigger SJW, which buys oscillator tolerance. */
    CHECK_NEAR(t.sample_point_permil, 875, 50);
    CHECK_EQ(1 + t.tseg1 + t.tseg2, t.nominal_bt_tq);
    CHECK_EQ(FDCAN_CLK_HZ / (t.brp * t.nominal_bt_tq), 500000);
}

TEST(standard_rates_are_all_exact) {
    const uint32_t rates[] = {125000, 250000, 500000, 1000000};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, rates[i], 875, false);
        CHECK(t.valid);
        CHECK_EQ(t.bitrate_error_ppm, 0);
        CHECK_NEAR(t.sample_point_permil, 875, 50);
    }
}

TEST(the_crystal_requirement_is_real) {
    /* This is design.md section 7.2 as an executable claim.  At 500 kbit/s
     * with a sensible sample point, the timing tolerates about 0.5% of
     * oscillator error per node -- which the HSI16's +/-1% does not fit inside
     * and a 30 ppm crystal fits inside with three orders of magnitude to
     * spare. */
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 500000, 875, false);
    const int tol = can_timing_max_tolerance_ppm(&t);

    CHECK_MSG(tol > 3000 && tol < 8000, "tolerance is around 0.5%");
    CHECK_MSG(!can_timing_osc_ok(&t, OSC_PPM_HSI16),
              "HSI16 at +/-1% must NOT pass");
    CHECK_MSG(can_timing_osc_ok(&t, OSC_PPM_CRYSTAL_30),
              "a 30 ppm crystal must pass");
    CHECK_MSG(can_timing_osc_ok(&t, OSC_PPM_CRYSTAL_50),
              "a 50 ppm crystal must pass");
}

TEST(hsi_fails_at_every_standard_rate) {
    /* Not just at 500k: the point is that the internal oscillator is never
     * good enough, so there is no bit rate where skipping the crystal is fine. */
    const uint32_t rates[] = {125000, 250000, 500000, 1000000};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, rates[i], 875, false);
        CHECK(t.valid);
        CHECK_MSG(!can_timing_osc_ok(&t, OSC_PPM_HSI16),
                  "HSI16 must fail at every standard rate");
    }
}

TEST(fd_data_phase_solves_within_its_tighter_limits) {
    /* The data phase registers are much smaller: DTSEG1 max 32, DTSEG2 max 16,
     * DBRP max 32.  A solver that ignores that produces register values that
     * silently truncate. */
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 2000000, 800, true);
    CHECK(t.valid);
    CHECK(t.tseg1 <= FDCAN_DTSEG1_MAX);
    CHECK(t.tseg2 <= FDCAN_DTSEG2_MAX);
    CHECK(t.brp <= FDCAN_DBRP_MAX);
    CHECK(t.sjw <= FDCAN_DSJW_MAX);
    CHECK_EQ(t.bitrate_error_ppm, 0);
    CHECK_EQ(t.bitrate_bps, 2000000);
    /* And with enough time quanta to be useful.  An exact sample point built
     * from five tq has an SJW of 1 and no room for delay compensation, and
     * the solver will happily find one if resolution is not ranked above
     * sample point -- which is what RESOLUTION_ENOUGH_TQ is for. */
    CHECK_MSG(t.nominal_bt_tq >= 16, "data phase needs real resolution");

    /* SJW is asserted through what it buys rather than as a bare number.  An
     * earlier version of this test demanded sjw >= 4, which was really an
     * assertion about a since-removed tie band in the solver: it had been
     * trading sample point for phase segment 2 width.  What actually matters
     * is that the timing survives the crystal on the board with room to
     * spare, and at 2 Mbit/s it does, by two orders of magnitude. */
    CHECK(t.sjw >= 2);
    CHECK_MSG(can_timing_osc_ok(&t, OSC_PPM_CRYSTAL_50),
              "the data phase must tolerate a 50 ppm crystal");
    CHECK_MSG(can_timing_max_tolerance_ppm(&t) > 20 * OSC_PPM_CRYSTAL_50,
              "and with real margin, not marginally");
}

TEST(fd_at_5mbit_still_fits) {
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 5000000, 750, true);
    CHECK(t.valid);
    CHECK_EQ(t.bitrate_bps, 5000000);
    CHECK(t.nominal_bt_tq >= 4);
}

TEST(sjw_is_as_large_as_it_can_be) {
    /* SJW buys oscillator tolerance directly and costs nothing, so leaving it
     * at 1 -- which plenty of generated configurations do -- throws away most
     * of the margin the crystal was bought for. */
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 500000, 875, false);
    CHECK_EQ(t.sjw, t.tseg2 < FDCAN_NSJW_MAX ? t.tseg2 : FDCAN_NSJW_MAX);
}

TEST(nbtp_register_encoding_is_value_minus_one) {
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 500000, 875, false);
    const uint32_t reg = can_timing_nbtp(&t);

    CHECK_EQ(((reg >> 25) & 0x7F) + 1, t.sjw);
    CHECK_EQ(((reg >> 16) & 0x1FF) + 1, t.brp);
    CHECK_EQ(((reg >> 8) & 0xFF) + 1, t.tseg1);
    CHECK_EQ((reg & 0x7F) + 1, t.tseg2);
}

TEST(dbtp_register_encoding_and_tdc_bit) {
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 2000000, 800, true);
    const uint32_t without = can_timing_dbtp(&t, false);
    const uint32_t with = can_timing_dbtp(&t, true);

    CHECK_EQ(((without >> 16) & 0x1F) + 1, t.brp);
    CHECK_EQ(((without >> 8) & 0x1F) + 1, t.tseg1);
    CHECK_EQ(((without >> 4) & 0xF) + 1, t.tseg2);
    CHECK_EQ((without & 0xF) + 1, t.sjw);
    CHECK_EQ((without >> 23) & 1, 0);
    CHECK_EQ((with >> 23) & 1, 1);
}

TEST(tdco_accounts_for_transceiver_loop_delay) {
    /* A TCAN1042's loop delay is around 110 ns.  Above roughly 1 Mbit/s the
     * secondary sample point has to move or the delay eats it. */
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 2000000, 800, true);
    const uint32_t tdco = can_timing_tdco(&t, 110, FDCAN_CLK_HZ);
    CHECK(tdco > 0);
    CHECK(tdco <= 127);
    /* 110 ns at 170 MHz is about 18 clock cycles, plus the data phase's own
     * sample point. */
    CHECK(tdco >= 18);
}

TEST(impossible_requests_fail_cleanly) {
    can_timing_t t = can_timing_solve(0, 500000, 875, false);
    CHECK(!t.valid);
    t = can_timing_solve(FDCAN_CLK_HZ, 0, 875, false);
    CHECK(!t.valid);
    /* Faster than the clock can express. */
    t = can_timing_solve(8000000, 8000000, 875, false);
    CHECK(!t.valid);
    CHECK_EQ(can_timing_max_tolerance_ppm(&t), 0);
}

TEST(a_bad_sample_point_request_falls_back_to_the_ciA_default) {
    can_timing_t t = can_timing_solve(FDCAN_CLK_HZ, 500000, 0, false);
    CHECK(t.valid);
    CHECK_NEAR(t.sample_point_permil, 875, 20);
}

TEST(sample_point_is_configurable_for_long_buses) {
    /* Long buses want a later sample point: propagation delay eats the
     * beginning of the bit. */
    can_timing_t late = can_timing_solve(FDCAN_CLK_HZ, 125000, 900, false);
    can_timing_t early = can_timing_solve(FDCAN_CLK_HZ, 125000, 750, false);
    CHECK(late.valid && early.valid);
    CHECK(late.sample_point_permil > early.sample_point_permil);
    CHECK_EQ(late.bitrate_error_ppm, 0);
    CHECK_EQ(early.bitrate_error_ppm, 0);
}

TEST_MAIN("can_timing", {
    RUN(classic_500k_is_exact_at_170mhz);
    RUN(standard_rates_are_all_exact);
    RUN(the_crystal_requirement_is_real);
    RUN(hsi_fails_at_every_standard_rate);
    RUN(fd_data_phase_solves_within_its_tighter_limits);
    RUN(fd_at_5mbit_still_fits);
    RUN(sjw_is_as_large_as_it_can_be);
    RUN(nbtp_register_encoding_is_value_minus_one);
    RUN(dbtp_register_encoding_and_tdc_bit);
    RUN(tdco_accounts_for_transceiver_loop_delay);
    RUN(impossible_requests_fail_cleanly);
    RUN(a_bad_sample_point_request_falls_back_to_the_ciA_default);
    RUN(sample_point_is_configurable_for_long_buses);
})
