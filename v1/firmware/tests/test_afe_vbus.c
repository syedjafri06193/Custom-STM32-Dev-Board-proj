/* Analog front end scaling, NAMUR fault detection, and VBUS monitoring
 * (design.md sections 6.6, 8.1, 8.2, 8.4, 16). */

#include "afe.h"
#include "test.h"
#include "vbus.h"

/* ---------------------------------------------------------------- 4-20 mA */

TEST(loop_endpoints_land_where_the_design_says) {
    /* 100 ohm shunt: 4 mA -> 0.4 V, 20 mA -> 2.0 V, inside the 2.048 V ref. */
    afe_channel_cfg_t cfg = afe_default_4_20ma();
    CHECK_EQ(afe_current_to_uv(&cfg, 4000), 400000);
    CHECK_EQ(afe_current_to_uv(&cfg, 20000), 2000000);
    CHECK_MSG(afe_current_to_uv(&cfg, 20000) < 2048000,
              "full scale must fit under the internal reference");
}

TEST(span_maps_to_percent) {
    afe_channel_cfg_t cfg = afe_default_4_20ma();

    afe_reading_t lo = afe_convert(&cfg, 400000);   /* 4 mA */
    afe_reading_t mid = afe_convert(&cfg, 1200000); /* 12 mA */
    afe_reading_t hi = afe_convert(&cfg, 2000000);  /* 20 mA */

    CHECK_EQ(lo.microamps, 4000);
    CHECK_EQ(mid.microamps, 12000);
    CHECK_EQ(hi.microamps, 20000);
    CHECK_EQ(lo.percent_milli, 0);
    CHECK_EQ(mid.percent_milli, 50000);
    CHECK_EQ(hi.percent_milli, 100000);
    CHECK_EQ(lo.status, AFE_OK);
    CHECK_EQ(mid.status, AFE_OK);
    CHECK_EQ(hi.status, AFE_OK);
}

TEST(engineering_units_scale_with_the_span) {
    /* A 0-250 degC transmitter. */
    afe_channel_cfg_t cfg = afe_default_4_20ma();
    cfg.eu_at_min_milli = 0;
    cfg.eu_at_max_milli = 250000;

    CHECK_EQ(afe_convert(&cfg, 400000).eu_milli, 0);
    CHECK_EQ(afe_convert(&cfg, 1200000).eu_milli, 125000);
    CHECK_EQ(afe_convert(&cfg, 2000000).eu_milli, 250000);

    /* And an inverted span, which real transmitters do use. */
    cfg.eu_at_min_milli = 100000;
    cfg.eu_at_max_milli = -50000;
    CHECK_EQ(afe_convert(&cfg, 400000).eu_milli, 100000);
    CHECK_EQ(afe_convert(&cfg, 2000000).eu_milli, -50000);
}

TEST(namur_thresholds_distinguish_fault_from_out_of_range) {
    /* NE43: a live transmitter never sits below 3.6 mA or above 21 mA, so
     * those regions mean something specific.  Reporting "0.0 units" for a cut
     * cable is how a logger lies to you. */
    afe_channel_cfg_t cfg = afe_default_4_20ma();

    CHECK_EQ(afe_convert(&cfg, 0).status, AFE_FAULT_LOW);          /* open loop */
    CHECK_EQ(afe_convert(&cfg, 350000).status, AFE_FAULT_LOW);     /* 3.5 mA */
    CHECK_EQ(afe_convert(&cfg, 370000).status, AFE_UNDERRANGE);    /* 3.7 mA */
    CHECK_EQ(afe_convert(&cfg, 2050000).status, AFE_OVERRANGE);    /* 20.5 mA */
    CHECK_EQ(afe_convert(&cfg, 2200000).status, AFE_FAULT_HIGH);   /* 22 mA */
}

TEST(out_of_range_readings_still_report_a_value) {
    /* The number is what shows the trend that explains the fault. */
    afe_channel_cfg_t cfg = afe_default_4_20ma();
    afe_reading_t r = afe_convert(&cfg, 2200000);
    CHECK_EQ(r.status, AFE_FAULT_HIGH);
    CHECK_EQ(r.microamps, 22000);
    CHECK(r.percent_milli > 100000);
}

TEST(calibration_removes_offset_and_gain_error) {
    afe_channel_cfg_t cfg = afe_default_4_20ma();
    cfg.offset_uv = 1500;   /* 1.5 mV of measured offset */
    cfg.gain_ppm = 2000;    /* and 0.2% of gain error */

    /* A true 12 mA reading arrives as 1.2 V plus those errors. */
    const int32_t measured =
        1200000 + 1500 + (int32_t)((1200000LL * 2000) / 1000000);
    afe_reading_t r = afe_convert(&cfg, measured);
    CHECK_NEAR(r.microamps, 12000, 10);
}

TEST(zero_to_ten_volt_channel_undoes_the_divider) {
    afe_channel_cfg_t cfg = afe_default_0_10v();
    /* 10 V through a 1:6 divider arrives as 1.667 V. */
    afe_reading_t r = afe_convert(&cfg, 1666667);
    CHECK_NEAR(r.microvolts * 6, 10000000, 20);
    CHECK_EQ(r.status, AFE_OK);
    CHECK_NEAR(r.percent_milli, 100000, 100);

    /* And the fault case the design document calls out: someone applies the
     * 24 V loop supply to a 0-10 V input. */
    afe_reading_t fault = afe_convert(&cfg, 4000000); /* 24 V at the terminal */
    CHECK_EQ(fault.status, AFE_FAULT_HIGH);
}

/* ------------------------------------------------------------ noise floor */

TEST(noise_free_bits_matches_the_section_16_formula) {
    /* noise_free_bits = log2(FSR / (6.6 * rms)).
     * FSR at PGA=1 is +/-2.048 V, so 4.096 V peak to peak. */
    const int32_t fsr_uv = 4096000;

    /* 1 uV RMS: log2(4096000 / 6.6) = log2(620606.06) = 19.2433 bits.  The
     * fixed-point implementation is checked against libm separately and is
     * good to a millibit, so this asserts the real number, not a ballpark. */
    CHECK_NEAR(afe_noise_free_millibits(fsr_uv, 1), 19243, 2);

    /* 10 uV RMS costs exactly log2(10) = 3.3219 bits, whatever the FSR. */
    const int32_t a = afe_noise_free_millibits(fsr_uv, 1);
    const int32_t b = afe_noise_free_millibits(fsr_uv, 10);
    CHECK_NEAR(a - b, 3322, 2);
    CHECK_NEAR(afe_noise_free_millibits(2048000, 1) -
                   afe_noise_free_millibits(2048000, 10),
               3322, 2);

    /* Halving the noise buys exactly one bit. */
    CHECK_EQ(afe_noise_free_millibits(fsr_uv, 1) -
                 afe_noise_free_millibits(fsr_uv, 2),
             1000);
}

TEST(the_isolated_dcdc_noise_expectation_is_reproducible) {
    /* Section 8.4: a competently executed board gets 18-19 noise-free bits,
     * not 24.  Working backwards, that is a few microvolts RMS -- which is
     * what this asserts, so the expectation is a number rather than a mood. */
    const int32_t fsr_uv = 4096000;

    const int32_t clean = afe_noise_free_millibits(fsr_uv, 2);   /* bench supply */
    const int32_t noisy = afe_noise_free_millibits(fsr_uv, 6);   /* iso DC-DC */

    CHECK(clean >= 18000 && clean <= 19000);
    CHECK(noisy >= 16000 && noisy <= 18000);
    CHECK_MSG(clean - noisy > 1000, "the coupling should cost more than a bit");
}

TEST(degenerate_noise_inputs_are_safe) {
    CHECK_EQ(afe_noise_free_millibits(4096000, 0), 0);
    CHECK_EQ(afe_noise_free_millibits(0, 5), 0);
    CHECK_EQ(afe_noise_free_millibits(4096000, 4096000), 0);
}

TEST(error_budget_adds_the_contributors) {
    /* 0.1% shunt, 25 ppm/C over 40 C, 0.2% reference, 0.05% ADC gain. */
    const int32_t ppm = afe_error_budget_ppm(1000, 25, 40, 2000, 500);
    CHECK_EQ(ppm, 1000 + 1000 + 2000 + 500);
    /* Which is 0.45% of span -- and the shunt's tempco alone is as large as
     * its tolerance once the board warms up. */
    CHECK(ppm < 10000);
}

/* ------------------------------------------------------------------ VBUS */

TEST(divider_maps_24v_to_three_volts) {
    vbus_divider_t d = vbus_default_divider();
    const uint32_t counts_24v = vbus_counts_from_mv(&d, 24000);
    const uint32_t full = (1u << d.adc_bits) - 1;

    CHECK(counts_24v < full);
    CHECK_MSG(counts_24v > (full * 85) / 100, "24 V should use most of the range");
    CHECK_NEAR(vbus_mv_from_counts(&d, counts_24v), 24000, 20);
}

TEST(round_trip_at_every_spr_voltage) {
    vbus_divider_t d = vbus_default_divider();
    const uint32_t volts[] = {5000, 9000, 12000, 15000, 20000};
    for (unsigned i = 0; i < sizeof(volts) / sizeof(volts[0]); i++) {
        const uint32_t counts = vbus_counts_from_mv(&d, volts[i]);
        CHECK_NEAR(vbus_mv_from_counts(&d, counts), volts[i], 25);
    }
}

TEST(full_scale_leaves_headroom_over_the_spr_ceiling) {
    vbus_divider_t d = vbus_default_divider();
    CHECK_MSG(vbus_full_scale_mv(&d) > 21000,
              "20 V plus margin must not clip the ADC");
}

TEST(classification_covers_the_bring_up_cases) {
    /* The tolerance argument is per-mil, and 50 is the +/-5% that USB PD
     * itself allows on a settled contract. */
    CHECK_EQ(vbus_classify(0, 5000, 50), VBUS_ABSENT);
    CHECK_EQ(vbus_classify(5000, 5000, 50), VBUS_OK);
    CHECK_EQ(vbus_classify(4000, 5000, 50), VBUS_LOW);
    CHECK_EQ(vbus_classify(20000, 20000, 50), VBUS_OK);

    /* 19 V on a 20 V contract is inside spec and must not raise an alarm;
     * 18 V is a source that is not holding the contract it agreed to. */
    CHECK_EQ(vbus_classify(19000, 20000, 50), VBUS_OK);
    CHECK_EQ(vbus_classify(18000, 20000, 50), VBUS_LOW);

    /* The one that should never happen: above the SPR ceiling means either the
     * EPR invariant was broken or the source is misbehaving. */
    CHECK_EQ(vbus_classify(28000, 20000, 50), VBUS_OVER_SPR);
    CHECK_STR(vbus_status_str(VBUS_OVER_SPR),
              "OVER 21 V -- SPR invariant violated");
}

TEST(divider_quiescent_current_is_microamps) {
    /* Section 6.6 asks for a divider sized for microamps.  At 20 V: */
    vbus_divider_t d = vbus_default_divider();
    const uint32_t total_ohms = d.r_top_ohms + d.r_bottom_ohms;
    const uint32_t microamps = (20000u * 1000u) / total_ohms;
    CHECK(microamps < 25);
}

TEST(sampling_time_requirement_forces_the_filter_cap) {
    /* The other half of that trade: a megohm divider means the ADC needs
     * microseconds of sampling time, which the default setting does not give.
     * The filter cap at the pin is what makes it work. */
    vbus_divider_t d = vbus_default_divider();
    const uint32_t z = vbus_source_impedance_ohms(&d);
    CHECK_NEAR(z, 125000, 5000); /* 1 M in parallel with 143 k */

    const uint32_t ns = vbus_min_sampling_ns(z, 1000, 5, 12);
    CHECK_MSG(ns > 4000, "over 4 us: far longer than a default sampling time");

    CHECK_MSG(!vbus_filter_cap_adequate(0, 5), "no cap is not adequate");
    CHECK_MSG(vbus_filter_cap_adequate(100, 5), "100 nF at the pin is");
}

TEST_MAIN("afe + vbus", {
    RUN(loop_endpoints_land_where_the_design_says);
    RUN(span_maps_to_percent);
    RUN(engineering_units_scale_with_the_span);
    RUN(namur_thresholds_distinguish_fault_from_out_of_range);
    RUN(out_of_range_readings_still_report_a_value);
    RUN(calibration_removes_offset_and_gain_error);
    RUN(zero_to_ten_volt_channel_undoes_the_divider);
    RUN(noise_free_bits_matches_the_section_16_formula);
    RUN(the_isolated_dcdc_noise_expectation_is_reproducible);
    RUN(degenerate_noise_inputs_are_safe);
    RUN(error_budget_adds_the_contributors);
    RUN(divider_maps_24v_to_three_volts);
    RUN(round_trip_at_every_spr_voltage);
    RUN(full_scale_leaves_headroom_over_the_spr_ceiling);
    RUN(classification_covers_the_bring_up_cases);
    RUN(divider_quiescent_current_is_microamps);
    RUN(sampling_time_requirement_forces_the_filter_cap);
})
