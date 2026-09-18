/* ADS1220 register encoding, conversion maths, and the SPI transport --
 * exercised against a fake that behaves like the part (design.md 4.2, 8.2). */

#include "ads1220.h"
#include "test.h"

/* A fake ADS1220 on the far side of the isolator: holds four registers and
 * returns a programmable conversion result. */
typedef struct {
    uint8_t regs[4];
    int32_t next_code;
    int cs_asserts;
    int reset_count;
    int start_count;
    bool cs_high_between_transfers;
} fake_adc_t;

static int fake_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    fake_adc_t *f = (fake_adc_t *)ctx;
    const uint8_t cmd = tx[0];

    if (cmd == ADS1220_CMD_RESET) {
        f->reset_count++;
        for (int i = 0; i < 4; i++) {
            f->regs[i] = 0; /* datasheet: all registers clear on reset */
        }
        return 0;
    }
    if (cmd == ADS1220_CMD_START) {
        f->start_count++;
        return 0;
    }
    if ((cmd & 0xF0) == ADS1220_CMD_WREG) {
        const int start = (cmd >> 2) & 0x3;
        const int count = (cmd & 0x3) + 1;
        for (int i = 0; i < count && (start + i) < 4; i++) {
            f->regs[start + i] = tx[1 + i];
        }
        return 0;
    }
    if ((cmd & 0xF0) == ADS1220_CMD_RREG) {
        const int start = (cmd >> 2) & 0x3;
        const int count = (cmd & 0x3) + 1;
        if (rx) {
            rx[0] = 0xFF;
            for (int i = 0; i < count && (start + i) < 4; i++) {
                rx[1 + i] = f->regs[start + i];
            }
        }
        return 0;
    }
    if (cmd == ADS1220_CMD_RDATA) {
        if (rx && len >= 4) {
            const uint32_t code = (uint32_t)f->next_code & 0xFFFFFF;
            rx[0] = 0xFF;
            rx[1] = (uint8_t)(code >> 16);
            rx[2] = (uint8_t)(code >> 8);
            rx[3] = (uint8_t)code;
        }
        return 0;
    }
    return -1;
}

static void fake_cs(void *ctx, bool asserted) {
    fake_adc_t *f = (fake_adc_t *)ctx;
    if (asserted) {
        f->cs_asserts++;
    }
}

static fake_adc_t g_fake;

static ads1220_t make_dev(void) {
    g_fake = (fake_adc_t){0};
    ads1220_t dev = {
        .xfer = fake_xfer,
        .cs = fake_cs,
        .ctx = &g_fake,
    };
    return dev;
}

static ads1220_config_t loop_config(void) {
    ads1220_config_t cfg = {
        .mux = ADS1220_MUX_AIN0_AVSS,
        .gain = ADS1220_GAIN_1,
        .pga_bypass = false,
        .rate = ADS1220_DR_20SPS,
        .continuous = true,
        .temperature_sensor = false,
        .burnout_current = false,
        .vref = ADS1220_VREF_INTERNAL,
        .filter = ADS1220_FILTER_50_60HZ,
        .external_vref_mv = 0,
    };
    return cfg;
}

TEST(register_encoding_round_trips) {
    ads1220_config_t cfg = loop_config();
    uint8_t regs[4];
    ads1220_encode(&cfg, regs);

    ads1220_config_t back;
    ads1220_decode(regs, &back);

    CHECK_EQ(back.mux, cfg.mux);
    CHECK_EQ(back.gain, cfg.gain);
    CHECK_EQ(back.rate, cfg.rate);
    CHECK_EQ(back.continuous, cfg.continuous);
    CHECK_EQ(back.vref, cfg.vref);
    CHECK_EQ(back.filter, cfg.filter);
}

TEST(register_bit_positions_match_the_datasheet) {
    ads1220_config_t cfg = loop_config();
    cfg.mux = ADS1220_MUX_AIN1_AVSS; /* 0x9 */
    cfg.gain = ADS1220_GAIN_16;      /* 0x4 */
    cfg.rate = ADS1220_DR_90SPS;     /* 0x2 */
    uint8_t regs[4];
    ads1220_encode(&cfg, regs);

    /* Reg0 = MUX[7:4] GAIN[3:1] PGA_BYPASS[0] */
    CHECK_EQ(regs[0], (0x9 << 4) | (0x4 << 1) | 0);
    /* Reg1 = DR[7:5] MODE[4:3] CM[2] TS[1] BCS[0]; continuous -> bit 2 */
    CHECK_EQ(regs[1], (0x2 << 5) | (1 << 2));
    /* Reg2 = VREF[7:6] 50/60[5:4] ... internal vref, 50/60 Hz filter */
    CHECK_EQ(regs[2], (0x0 << 6) | (0x1 << 4));
    CHECK_EQ(regs[3], 0);
}

TEST(gain_and_rate_lookups) {
    CHECK_EQ(ads1220_gain_value(ADS1220_GAIN_1), 1);
    CHECK_EQ(ads1220_gain_value(ADS1220_GAIN_16), 16);
    CHECK_EQ(ads1220_gain_value(ADS1220_GAIN_128), 128);
    CHECK_EQ(ads1220_rate_sps(ADS1220_DR_20SPS), 20);
    CHECK_EQ(ads1220_rate_sps(ADS1220_DR_1000SPS), 1000);
}

TEST(sign_extension_of_24_bit_results) {
    CHECK_EQ(ads1220_sign_extend(0x000000), 0);
    CHECK_EQ(ads1220_sign_extend(0x7FFFFF), 8388607);
    CHECK_EQ(ads1220_sign_extend(0x800000), -8388608);
    CHECK_EQ(ads1220_sign_extend(0xFFFFFF), -1);
}

TEST(conversion_maths_matches_the_shunt_design) {
    /* Section 8.2: 4-20 mA through 100 ohm is 0.4-2.0 V, chosen to sit inside
     * the 2.048 V internal reference at PGA = 1. */
    const int32_t code_400mv =
        ads1220_uv_to_code(400000, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1);
    const int32_t code_2v =
        ads1220_uv_to_code(2000000, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1);

    CHECK(code_2v < ADS1220_FULL_SCALE_COUNTS);
    CHECK_MSG(code_2v > (ADS1220_FULL_SCALE_COUNTS * 9) / 10,
              "2.0 V should use most of the range");

    CHECK_NEAR(ads1220_code_to_uv(code_400mv, ADS1220_INTERNAL_VREF_MV,
                                  ADS1220_GAIN_1),
               400000, 2);
    CHECK_NEAR(ads1220_code_to_uv(code_2v, ADS1220_INTERNAL_VREF_MV,
                                  ADS1220_GAIN_1),
               2000000, 2);
}

TEST(gain_scales_the_conversion) {
    const int32_t uv = 10000; /* 10 mV */
    const int32_t g1 = ads1220_uv_to_code(uv, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1);
    const int32_t g16 = ads1220_uv_to_code(uv, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_16);
    CHECK_NEAR(g16, g1 * 16, 16);
    CHECK_NEAR(ads1220_code_to_uv(g16, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_16),
               uv, 2);
}

TEST(negative_inputs_convert_symmetrically) {
    const int32_t code =
        ads1220_uv_to_code(-500000, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1);
    CHECK(code < 0);
    CHECK_NEAR(ads1220_code_to_uv(code, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1),
               -500000, 2);
}

TEST(init_resets_configures_and_starts) {
    ads1220_t dev = make_dev();
    ads1220_config_t cfg = loop_config();

    CHECK_EQ(ads1220_init(&dev, &cfg), 0);
    CHECK_EQ(g_fake.reset_count, 1);
    CHECK_EQ(g_fake.start_count, 1); /* continuous mode: START is required */

    /* And the part now holds what we asked for -- which is the check that
     * proves the isolator works in both directions during bring-up. */
    uint8_t regs[4];
    CHECK_EQ(ads1220_read_regs(&dev, regs), 0);
    uint8_t expect[4];
    ads1220_encode(&cfg, expect);
    CHECK_EQ(regs[0], expect[0]);
    CHECK_EQ(regs[1], expect[1]);
    CHECK_EQ(regs[2], expect[2]);
    CHECK_EQ(regs[3], expect[3]);
}

TEST(single_shot_mode_does_not_start_automatically) {
    ads1220_t dev = make_dev();
    ads1220_config_t cfg = loop_config();
    cfg.continuous = false;
    CHECK_EQ(ads1220_init(&dev, &cfg), 0);
    CHECK_EQ(g_fake.start_count, 0);
}

TEST(read_raw_recovers_the_conversion) {
    ads1220_t dev = make_dev();
    ads1220_config_t cfg = loop_config();
    ads1220_init(&dev, &cfg);

    g_fake.next_code = 4194304; /* half of full scale */
    int32_t code = 0;
    CHECK_EQ(ads1220_read_raw(&dev, &code), 0);
    CHECK_EQ(code, 4194304);

    g_fake.next_code = -1;
    CHECK_EQ(ads1220_read_raw(&dev, &code), 0);
    CHECK_EQ(code, -1);
}

TEST(set_mux_preserves_the_rest_of_the_configuration) {
    ads1220_t dev = make_dev();
    ads1220_config_t cfg = loop_config();
    cfg.gain = ADS1220_GAIN_4;
    ads1220_init(&dev, &cfg);

    CHECK_EQ(ads1220_set_mux(&dev, ADS1220_MUX_AIN2_AVSS), 0);

    uint8_t regs[4];
    ads1220_read_regs(&dev, regs);
    ads1220_config_t back;
    ads1220_decode(regs, &back);
    CHECK_EQ(back.mux, ADS1220_MUX_AIN2_AVSS);
    CHECK_EQ(back.gain, ADS1220_GAIN_4);
    CHECK_EQ(back.rate, cfg.rate);
}

TEST(chip_select_is_asserted_once_per_transaction) {
    /* The ADS1220 latches on CS rising; leaving it low across transactions is
     * a classic way to get a part that answers once and then goes quiet. */
    ads1220_t dev = make_dev();
    ads1220_config_t cfg = loop_config();
    ads1220_init(&dev, &cfg);
    const int after_init = g_fake.cs_asserts;
    CHECK(after_init >= 3); /* reset, write config, start */

    int32_t code;
    ads1220_read_raw(&dev, &code);
    CHECK_EQ(g_fake.cs_asserts, after_init + 1);
}

TEST(null_arguments_do_not_crash) {
    CHECK_EQ(ads1220_init(NULL, NULL), -1);
    ads1220_t dev = make_dev();
    CHECK_EQ(ads1220_read_raw(&dev, NULL), -1);
    ads1220_encode(NULL, NULL);
    ads1220_decode(NULL, NULL);
}

TEST_MAIN("ads1220", {
    RUN(register_encoding_round_trips);
    RUN(register_bit_positions_match_the_datasheet);
    RUN(gain_and_rate_lookups);
    RUN(sign_extension_of_24_bit_results);
    RUN(conversion_maths_matches_the_shunt_design);
    RUN(gain_scales_the_conversion);
    RUN(negative_inputs_convert_symmetrically);
    RUN(init_resets_configures_and_starts);
    RUN(single_shot_mode_does_not_start_automatically);
    RUN(read_raw_recovers_the_conversion);
    RUN(set_mux_preserves_the_rest_of_the_configuration);
    RUN(chip_select_is_asserted_once_per_transaction);
    RUN(null_arguments_do_not_crash);
})
