/* Bring-up self-test.
 *
 * This is section 15 of the design document as a program.  It runs the stages
 * in the order the document gives them, and it stops at the first stage that
 * fails rather than continuing to the next.
 *
 * The ordering is the whole point.  "Never plug a new board into USB first"
 * is the document's first line, and the reason each stage comes before the
 * next is that a failure in stage N makes stage N+1's result meaningless.
 * A CAN loopback that fails because the crystal is not running looks exactly
 * like a CAN loopback that fails because the bit timing is wrong, and the
 * only way to tell them apart is to have already proved the crystal.
 *
 * Every stage prints a number, not a verdict.  "CRYSTAL OK" tells you
 * nothing; "MCO 10.625 MHz expected, measure it" tells you what to compare
 * against, and the ones the firmware can measure itself it prints with the
 * bound it was checked against.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ads1220.h"
#include "afe.h"
#include "board.h"
#include "can_timing.h"
#include "hal.h"
#include "pd_policy.h"
#include "rcc.h"
#include "vbus.h"

/* ------------------------------------------------------------- reporting */

static int s_stage;
static int s_failures;

static void stage(const char *name) {
    s_stage++;
    printf("\n=== Stage %d: %s ===\n", s_stage, name);
}

static bool check(const char *what, bool ok, const char *detail) {
    printf("  [%s] %-38s %s\n", ok ? "PASS" : "FAIL", what,
           detail ? detail : "");
    if (!ok) {
        s_failures++;
        led_set(LED_FAULT, true);
    }
    return ok;
}

/* ---------------------------------------------------- stage 2: the MCU */

static bool stage_mcu(void) {
    stage("MCU and clocks (procedure steps 10-12)");

    /* Getting this far means the clock init returned 0 and the console is
     * transmitting at the baud rate computed from BOARD_PCLK1_HZ.  If the PLL
     * were not actually at 170 MHz this text would be garbled -- so legible
     * output is itself the first measurement. */
    printf("  SYSCLK        %lu Hz\n", (unsigned long)g_sysclk_hz);
    printf("  HSE crystal   %lu Hz\n", (unsigned long)BOARD_HSE_HZ);

    rcc_mco_enable();
    printf("  MCO on PA8    %lu Hz expected -- measure this, do NOT probe\n",
           (unsigned long)rcc_mco_expected_hz());
    printf("                the crystal pins; the probe load can stop it.\n");

    /* Step 11: blink an LED.  Cheap, and it is the only evidence available
     * when the UART turns out to be the thing that is broken. */
    for (int i = 0; i < 6; i++) {
        led_toggle(LED_USER);
        delay_ms(80);
    }
    led_set(LED_USER, true);

    return check("clock tree at 170 MHz", g_sysclk_hz == BOARD_SYSCLK_HZ, "");
}

/* --------------------------------------------- stage 3: USB-C and VBUS */

static bool stage_usb_c(void) {
    stage("USB-C, CC lines and VBUS (procedure steps 13-16)");

    const cc_line_t line = ucpd_attached_line();
    const char *orientation = (line == CC_1)   ? "CC1"
                              : (line == CC_2) ? "CC2"
                                               : "none";
    printf("  CC1 vstate    %u\n", ucpd_vstate(CC_1));
    printf("  CC2 vstate    %u\n", ucpd_vstate(CC_2));
    printf("  orientation   %s\n", orientation);

    const uint32_t mv = vbus_read_mv();
    vbus_divider_t d = vbus_default_divider();
    printf("  VBUS          %lu mV (divider full scale %lu mV)\n",
           (unsigned long)mv, (unsigned long)vbus_full_scale_mv(&d));

    /* Step 13 is "first with a dumb 5 V source", so 5 V is a pass, not a
     * disappointment.  The board is required to work on one. */
    const vbus_status_t st = vbus_classify(mv, 5000, 50);
    printf("  vs 5 V        %s\n", vbus_status_str(st));

    bool ok = check("a source is attached", line != CC_NONE,
                    line == CC_NONE ? "plug in a cable" : orientation);

    /* The invariant, checked against the actual rail rather than assumed.
     * If this ever fails the board is being fed above its input rating and
     * the right response is to stop, not to log and continue. */
    ok &= check("VBUS within the SPR ceiling", st != VBUS_OVER_SPR,
                st == VBUS_OVER_SPR ? "SPR INVARIANT VIOLATED -- unplug" : "");

    /* Section 6.3.  Without a PD protocol layer present there is no contract,
     * so the bulk switch must still be open.  This asserts the safe default
     * rather than trusting it. */
    ok &= check("bulk capacitance still gated",
                !bulk_switch_is_enabled(),
                "no contract yet: 470 uF stays off VBUS");

    return ok;
}

/* --------------------------------------------------------- stage 4: CAN */

static can_timing_t s_nominal;
static can_timing_t s_data;

static bool stage_can(void) {
    stage("CAN (procedure steps 17-21)");

    s_nominal = can_timing_solve(BOARD_FDCAN_CLK_HZ, BOARD_CAN_NOMINAL_BPS,
                                 BOARD_CAN_SAMPLE_POINT_PERMIL, false);
    s_data = can_timing_solve(BOARD_FDCAN_CLK_HZ, BOARD_CAN_DATA_BPS,
                              BOARD_CAN_DATA_SAMPLE_POINT_PERMIL, true);

    if (!check("nominal bit timing solved", s_nominal.valid, "")) {
        return false;
    }

    printf("  nominal       %lu bit/s, brp %lu, tseg1 %lu, tseg2 %lu, sjw %lu\n",
           (unsigned long)s_nominal.bitrate_bps, (unsigned long)s_nominal.brp,
           (unsigned long)s_nominal.tseg1, (unsigned long)s_nominal.tseg2,
           (unsigned long)s_nominal.sjw);
    printf("                sample point %lu.%lu%%, error %ld ppm\n",
           (unsigned long)(s_nominal.sample_point_permil / 10u),
           (unsigned long)(s_nominal.sample_point_permil % 10u),
           (long)s_nominal.bitrate_error_ppm);

    const int tol = can_timing_max_tolerance_ppm(&s_nominal);
    printf("  osc tolerance %d ppm required of each node\n", tol);
    printf("  crystal 30ppm %s | HSI16 +/-1%% %s\n",
           can_timing_osc_ok(&s_nominal, OSC_PPM_CRYSTAL_30) ? "OK" : "FAIL",
           can_timing_osc_ok(&s_nominal, OSC_PPM_HSI16) ? "OK" : "FAIL");

    bool ok = check("bit rate is exact", s_nominal.bitrate_error_ppm == 0, "");
    ok &= check("crystal meets CAN tolerance",
                can_timing_osc_ok(&s_nominal, OSC_PPM_CRYSTAL_30),
                "section 7.2");

    /* Step 17: internal loopback, no transceiver.  The transceiver stays in
     * standby for this so a wiring fault cannot put anything on a live bus
     * while the peripheral is being proved. */
    can_transceiver_standby(true);
    if (can_init(&s_nominal, &s_data, CAN_MODE_INTERNAL_LOOPBACK) != 0) {
        return check("FDCAN internal loopback init", false, "");
    }

    can_frame_t tx;
    memset(&tx, 0, sizeof(tx));
    tx.id = 0x123;
    tx.dlc = 8;
    tx.fd = true;
    tx.brs = true;
    for (int i = 0; i < 8; i++) {
        tx.data[i] = (uint8_t)(0xA0 + i);
    }

    ok &= check("frame queued", can_send(&tx) == 0, "");

    can_frame_t rx;
    int got = -1;
    const uint32_t start = millis();
    while ((millis() - start) < 50u) {
        got = can_recv(&rx);
        if (got == 0) {
            break;
        }
    }

    ok &= check("internal loopback round trip", got == 0, "step 17");
    if (got == 0) {
        ok &= check("id preserved", rx.id == tx.id, "");
        ok &= check("payload preserved",
                    rx.dlc == tx.dlc && memcmp(rx.data, tx.data, 8) == 0, "");
        ok &= check("FD and BRS preserved", rx.fd && rx.brs,
                    "data phase actually ran at 2 Mbit/s");
        led_toggle(LED_CAN);
    }

    uint8_t tec = 0, rec = 0;
    bool bus_off = false;
    can_error_counters(&tec, &rec, &bus_off);
    printf("  error counters TEC %u, REC %u, bus-off %s, LEC %u\n", tec, rec,
           bus_off ? "yes" : "no", can_last_error_code());

    ok &= check("no errors during loopback", tec == 0 && rec == 0 && !bus_off,
                "");

    /* Steps 18-21 need the transceiver and a real bus, so they are left to
     * the operator rather than faked.  The firmware sets up for step 18 and
     * says what to do next. */
    printf("\n  Next, by hand:\n");
    printf("    18. external loopback through the transceiver\n");
    printf("    19. two nodes, terminated, short cable\n");
    printf("    20. scope CANH/CANL -- recessive near 2.5 V on both\n");
    printf("    21. long cable at full rate, watch the error counters\n");

    return ok;
}

/* -------------------------------------------- stage 5: isolated front end */

static ads1220_t s_adc;

static bool stage_isolated_afe(void) {
    stage("Isolated analog front end (procedure steps 22-27)");

    /* Step 22: the island is bench-powered through the barrier jumper for
     * this first pass, with the isolated DC-DC off.  That is deliberate --
     * step 26 turns it on and repeats the measurement, and the difference
     * between the two is the number section 16 actually wants. */
    iso_power_enable(false);
    printf("  isolated DC-DC OFF -- island expected on the bench jumper\n");
    printf("  (bench jumper DEFEATS ISOLATION; bench use only)\n");
    delay_ms(50);

    iso_spi_init();
    iso_spi_bind(&s_adc);

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

    bool ok = check("ADS1220 init", ads1220_init(&s_adc, &cfg) == 0, "");

    /* Step 23: read the registers back.  This is the test that proves the
     * isolator works in BOTH directions -- a write that lands and a read that
     * returns it means three forward channels and two reverse channels are
     * all alive, which is the thing section 4.3 warns you to count. */
    uint8_t regs[4] = {0};
    uint8_t expect[4] = {0};
    ads1220_encode(&cfg, expect);
    ok &= check("register read-back over the isolator",
                ads1220_read_regs(&s_adc, regs) == 0 &&
                    memcmp(regs, expect, 4) == 0,
                "proves 3 forward + 2 reverse channels");
    printf("  regs          %02X %02X %02X %02X (expected %02X %02X %02X %02X)\n",
           regs[0], regs[1], regs[2], regs[3], expect[0], expect[1], expect[2],
           expect[3]);

    /* Step 24: shorted inputs, and the noise floor that comes out of it. */
    printf("  sampling noise floor (inputs shorted)...\n");
    int32_t samples[64];
    int n = 0;
    const uint32_t start = millis();
    while (n < 64 && (millis() - start) < 5000u) {
        if (!iso_adc_data_ready()) {
            continue;
        }
        if (ads1220_read_raw(&s_adc, &samples[n]) == 0) {
            n++;
        }
    }

    if (!check("conversions arriving", n >= 16, "check /DRDY wiring")) {
        return false;
    }

    int64_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum += samples[i];
    }
    const int32_t mean = (int32_t)(sum / n);

    /* Peak-to-peak rather than a true RMS: computing RMS needs a square root
     * and this firmware has no floating point on purpose.  p-p / 6.6 is the
     * same estimator the noise-free-bits formula uses in reverse, so the two
     * agree, and the real characterisation in section 16 belongs on a host
     * with the raw samples anyway -- which is what tools/noise_floor.py does. */
    int32_t lo = samples[0], hi = samples[0];
    for (int i = 1; i < n; i++) {
        if (samples[i] < lo) lo = samples[i];
        if (samples[i] > hi) hi = samples[i];
    }
    const int32_t pp_uv =
        ads1220_code_to_uv(hi, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1) -
        ads1220_code_to_uv(lo, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1);
    const int32_t rms_uv = pp_uv / 6 > 0 ? pp_uv / 6 : 1;
    const int32_t millibits = afe_noise_free_millibits(4096000, rms_uv);

    printf("  samples       %d, mean code %ld\n", n, (long)mean);
    printf("  noise p-p     %ld uV -> ~%ld uV RMS\n", (long)pp_uv,
           (long)rms_uv);
    printf("  noise-free    %ld.%03ld bits  [bench power]\n",
           (long)(millibits / 1000), (long)(millibits % 1000));

    /* Section 8.4: a competently built board lands at 18-19 noise-free bits,
     * not 24.  Flagging anything above 20 as suspicious is not pessimism --
     * it means the inputs are probably not actually shorted, or the part is
     * returning a constant. */
    ok &= check("noise floor is plausible",
                millibits > 12000 && millibits < 20500,
                millibits >= 20500 ? "too good: are the inputs really shorted?"
                                   : "");

    /* Step 26: now the converter, and the difference is the cost of
     * isolation.  This is the measurement almost nobody publishes. */
    printf("\n  Enabling the isolated DC-DC and repeating...\n");
    iso_power_enable(true);
    delay_ms(200);

    n = 0;
    const uint32_t start2 = millis();
    while (n < 64 && (millis() - start2) < 5000u) {
        if (!iso_adc_data_ready()) {
            continue;
        }
        if (ads1220_read_raw(&s_adc, &samples[n]) == 0) {
            n++;
        }
    }

    if (n >= 16) {
        lo = samples[0];
        hi = samples[0];
        for (int i = 1; i < n; i++) {
            if (samples[i] < lo) lo = samples[i];
            if (samples[i] > hi) hi = samples[i];
        }
        const int32_t pp2 =
            ads1220_code_to_uv(hi, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1) -
            ads1220_code_to_uv(lo, ADS1220_INTERNAL_VREF_MV, ADS1220_GAIN_1);
        const int32_t rms2 = pp2 / 6 > 0 ? pp2 / 6 : 1;
        const int32_t mb2 = afe_noise_free_millibits(4096000, rms2);
        printf("  noise-free    %ld.%03ld bits  [isolated DC-DC]\n",
               (long)(mb2 / 1000), (long)(mb2 % 1000));
        printf("  COST OF ISOLATION: %ld.%03ld bits\n",
               (long)((millibits - mb2) / 1000),
               (long)((millibits - mb2) % 1000));
    }

    /* Step 25/27: a known current, and then a real transmitter.  The scaling
     * and the NE43 decision are core/ code, so the only thing left for the
     * operator is to apply the current. */
    printf("\n  Apply a known loop current, then:\n");
    afe_channel_cfg_t loop = afe_default_4_20ma();
    printf("    4 mA  -> %ld uV at the ADC\n",
           (long)afe_current_to_uv(&loop, 4000));
    printf("    12 mA -> %ld uV\n", (long)afe_current_to_uv(&loop, 12000));
    printf("    20 mA -> %ld uV\n", (long)afe_current_to_uv(&loop, 20000));
    printf("    below 3.6 mA or above 21 mA is reported as a FAULT, not a\n");
    printf("    reading -- NE43.  A logger that prints 0.0 for a cut cable\n");
    printf("    is a logger that lies to you.\n");

    return ok;
}

/* ------------------------------------------------------------------ main */

int main(void) {
    const int rc = rcc_clock_init();

    gpio_init();
    delay_init();
    console_init(CONSOLE_BAUD);

    printf("\n\n");
    printf("========================================================\n");
    printf(" STM32G474 industrial logger -- bring-up self test\n");
    printf(" Functional isolation only. NOT rated for mains-\n");
    printf(" referenced sensors. Bench jumper defeats isolation.\n");
    printf("========================================================\n");

    if (rc != 0) {
        /* The clock failed, so everything below would be measuring the wrong
         * thing.  Say which failure it was -- these have different fixes. */
        const char *why = (rc == RCC_ERR_HSE)     ? "HSE crystal did not start"
                          : (rc == RCC_ERR_PLL)   ? "PLL did not lock"
                          : (rc == RCC_ERR_VOS)   ? "voltage scaling stuck"
                                                  : "flash latency rejected";
        printf("\nFATAL: %s (rc=%d)\n", why, rc);
        if (rc == RCC_ERR_HSE) {
            printf("  Check: load caps (CL = 2*(C_load - C_stray)), crystal\n");
            printf("  part number, solder joints.  This is the most common\n");
            printf("  first-assembly failure and it is not a firmware bug.\n");
        }
        for (;;) {
            led_toggle(LED_FAULT);
            delay_ms(120);
        }
    }

    ucpd_init();
    vbus_adc_init();

    bool ok = stage_mcu();
    if (ok) {
        ok = stage_usb_c();
    }
    if (ok) {
        ok = stage_can();
    }
    if (ok) {
        ok = stage_isolated_afe();
    }

    printf("\n========================================================\n");
    if (s_failures == 0) {
        printf(" All automated stages passed. Continue by hand from\n");
        printf(" the steps printed above.\n");
    } else {
        printf(" %d check(s) failed. Fix the FIRST failure above; the\n",
               s_failures);
        printf(" later stages assume the earlier ones are sound.\n");
    }
    printf("========================================================\n");

    for (;;) {
        led_toggle(s_failures == 0 ? LED_USER : LED_FAULT);
        delay_ms(s_failures == 0 ? 500 : 120);
    }
}
