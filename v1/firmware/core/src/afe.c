/* See afe.h. */

#include "afe.h"

#include <stddef.h>

afe_channel_cfg_t afe_default_4_20ma(void) {
    afe_channel_cfg_t cfg = {
        .type = AFE_INPUT_4_20MA,
        .shunt_milliohms = 100000u, /* 100 ohm, 0.1%, low tempco */
        .divider_num = 1,
        .divider_den = 1,
        .offset_uv = 0,
        .gain_ppm = 0,
        .eu_at_min_milli = 0,
        .eu_at_max_milli = 100000, /* 0-100.000 % by default */
    };
    return cfg;
}

afe_channel_cfg_t afe_default_0_10v(void) {
    afe_channel_cfg_t cfg = {
        .type = AFE_INPUT_0_10V,
        .shunt_milliohms = 0,
        /* 10 V into the ADS1220's 2.048 V reference needs roughly 1:6;
         * 1 M / 200 k gives 1:6 exactly with sensible resistor values. */
        .divider_num = 1,
        .divider_den = 6,
        .offset_uv = 0,
        .gain_ppm = 0,
        .eu_at_min_milli = 0,
        .eu_at_max_milli = 100000,
    };
    return cfg;
}

int32_t afe_current_to_uv(const afe_channel_cfg_t *cfg, int32_t microamps) {
    if (cfg == NULL || cfg->shunt_milliohms == 0) {
        return 0;
    }
    /* V = I * R: microamps * milliohms = nanovolts, so divide by 1000. */
    return (int32_t)(((int64_t)microamps * (int64_t)cfg->shunt_milliohms) / 1000);
}

/* How far past the end of span a reading may sit and still count as "at the
 * end of span".  Integer scaling rounds: a true 10.000000 V through a 1:6
 * divider comes back as 10.000002 V, and calling that over-range would put a
 * fault flag on a sensor sitting exactly at its maximum.  10 ppm of span is
 * far below the ADC's own noise floor, so nothing real hides inside it. */
static int32_t span_slack(int32_t lo, int32_t hi) {
    const int64_t span = (int64_t)hi - (int64_t)lo;
    const int64_t s = (span < 0 ? -span : span) / 100000;
    return s < 1 ? 1 : (int32_t)s;
}

static int32_t apply_calibration(const afe_channel_cfg_t *cfg, int32_t uv) {
    int64_t corrected = (int64_t)uv - cfg->offset_uv;
    if (cfg->gain_ppm != 0) {
        corrected -= (corrected * cfg->gain_ppm) / 1000000;
    }
    return (int32_t)corrected;
}

afe_reading_t afe_convert(const afe_channel_cfg_t *cfg, int32_t raw_microvolts) {
    afe_reading_t r = {0};
    if (cfg == NULL) {
        r.status = AFE_FAULT_LOW;
        return r;
    }

    r.microvolts = apply_calibration(cfg, raw_microvolts);

    int32_t span_lo, span_hi, value, slack;

    if (cfg->type == AFE_INPUT_4_20MA) {
        if (cfg->shunt_milliohms == 0) {
            r.status = AFE_FAULT_LOW;
            return r;
        }
        /* I = V / R, in microamps. */
        r.microamps =
            (int32_t)(((int64_t)r.microvolts * 1000) / (int64_t)cfg->shunt_milliohms);
        value = r.microamps;
        span_lo = AFE_LOOP_MIN_UA;
        span_hi = AFE_LOOP_MAX_UA;
        slack = span_slack(span_lo, span_hi);

        if (r.microamps < AFE_NAMUR_LOW_UA) {
            r.status = AFE_FAULT_LOW;
        } else if (r.microamps > AFE_NAMUR_HIGH_UA) {
            r.status = AFE_FAULT_HIGH;
        } else if (r.microamps < AFE_LOOP_MIN_UA - slack) {
            r.status = AFE_UNDERRANGE;
        } else if (r.microamps > AFE_LOOP_MAX_UA + slack) {
            r.status = AFE_OVERRANGE;
        } else {
            r.status = AFE_OK;
        }
    } else {
        /* 0-10 V through the divider: undo the attenuation. */
        const int64_t scaled =
            ((int64_t)r.microvolts * (int64_t)cfg->divider_den) /
            (int64_t)(cfg->divider_num ? cfg->divider_num : 1);
        value = (int32_t)scaled;
        span_lo = 0;
        span_hi = 10000000; /* 10 V in microvolts */
        slack = span_slack(span_lo, span_hi);
        r.microamps = 0;

        if (value < -100000) { /* more than -0.1 V: wired backwards */
            r.status = AFE_FAULT_LOW;
        } else if (value > 11000000) { /* over 11 V: out of range */
            r.status = AFE_FAULT_HIGH;
        } else if (value < -slack) {
            r.status = AFE_UNDERRANGE;
        } else if (value > span_hi + slack) {
            r.status = AFE_OVERRANGE;
        } else {
            r.status = AFE_OK;
        }
    }

    /* Percent of span, and engineering units, both in millis.  Computed even
     * for out-of-range readings: a logger that stops reporting numbers the
     * moment something is odd hides the trend that explains it. */
    const int64_t span = (int64_t)span_hi - (int64_t)span_lo;
    if (span != 0) {
        r.percent_milli =
            (int32_t)((((int64_t)value - span_lo) * 100000) / span);
        const int64_t eu_span =
            (int64_t)cfg->eu_at_max_milli - (int64_t)cfg->eu_at_min_milli;
        r.eu_milli =
            (int32_t)(cfg->eu_at_min_milli +
                      (((int64_t)value - span_lo) * eu_span) / span);
    }

    return r;
}

int32_t afe_noise_free_millibits(int32_t full_scale_uv, int32_t rms_noise_uv) {
    if (rms_noise_uv <= 0 || full_scale_uv <= 0) {
        return 0;
    }
    /* ratio = FSR / (6.6 * rms), and the answer is log2 of it.  No libm on the
     * target, so: take the integer part by halving, then the fraction by the
     * classic repeated-squaring method in Q30 fixed point.  Squaring a Q30
     * value that is under 2.0 stays under 2^62, so it never overflows and --
     * unlike rescaling a ratio of two large integers -- it loses no precision
     * as it goes. */
    uint64_t num = (uint64_t)full_scale_uv * 10u;
    uint64_t den = (uint64_t)rms_noise_uv * 66u;
    if (num <= den) {
        return 0;
    }

    int32_t bits = 0;
    while (num >= den * 2u && bits < 40) {
        den *= 2u;
        bits++;
    }

    /* Keep num under 2^30 so the Q30 normalisation below cannot overflow.
     * den tracks it, and both stay around 30 significant bits. */
    while (num >= ((uint64_t)1 << 30)) {
        num >>= 1;
        den >>= 1;
    }
    if (den == 0) {
        return bits * 1000;
    }

    /* x is num/den in Q30, so it lies in [1.0, 2.0). */
    uint64_t x = (num << 30) / den;

    /* Sixteen binary places of fraction, accumulated in Q16. */
    uint32_t frac_q16 = 0;
    for (int i = 0; i < 16; i++) {
        x = (x * x) >> 30;
        if (x >= ((uint64_t)1 << 31)) { /* squared past 2.0: this bit is set */
            x >>= 1;
            frac_q16 |= (uint32_t)1 << (15 - i);
        }
    }

    return bits * 1000 + (int32_t)(((uint64_t)frac_q16 * 1000u) >> 16);
}

int32_t afe_error_budget_ppm(uint32_t shunt_tolerance_ppm,
                             uint32_t shunt_tempco_ppm_per_c,
                             int32_t delta_temp_c, uint32_t vref_tolerance_ppm,
                             uint32_t adc_gain_error_ppm) {
    const int32_t dt = delta_temp_c < 0 ? -delta_temp_c : delta_temp_c;
    return (int32_t)shunt_tolerance_ppm +
           (int32_t)(shunt_tempco_ppm_per_c * (uint32_t)dt) +
           (int32_t)vref_tolerance_ppm + (int32_t)adc_gain_error_ppm;
}

const char *afe_status_str(afe_status_t s) {
    switch (s) {
        case AFE_OK: return "ok";
        case AFE_UNDERRANGE: return "under-range";
        case AFE_OVERRANGE: return "over-range";
        case AFE_FAULT_LOW: return "fault: below NE43 low (broken wire?)";
        case AFE_FAULT_HIGH: return "fault: above NE43 high (short?)";
        case AFE_SATURATED: return "fault: ADC saturated";
        default: return "unknown";
    }
}
