/* See vbus.h. */

#include "vbus.h"

#include <stddef.h>

#include "pd_policy.h"

vbus_divider_t vbus_default_divider(void) {
    vbus_divider_t d = {
        .r_top_ohms = 1000000u,
        .r_bottom_ohms = 143000u,
        .vref_mv = 3300,
        .adc_bits = 12,
    };
    return d;
}

uint32_t vbus_mv_from_counts(const vbus_divider_t *d, uint32_t counts) {
    if (d == NULL || d->adc_bits == 0 || d->r_bottom_ohms == 0) {
        return 0;
    }
    const uint32_t full = (1u << d->adc_bits) - 1u;
    if (counts > full) {
        counts = full;
    }
    /* pin voltage, then undo the divider */
    const uint64_t pin_uv = ((uint64_t)counts * d->vref_mv * 1000) / full;
    const uint64_t vbus_uv =
        (pin_uv * (d->r_top_ohms + d->r_bottom_ohms)) / d->r_bottom_ohms;
    return (uint32_t)(vbus_uv / 1000);
}

uint32_t vbus_counts_from_mv(const vbus_divider_t *d, uint32_t millivolts) {
    if (d == NULL || d->adc_bits == 0 || d->vref_mv == 0) {
        return 0;
    }
    const uint32_t full = (1u << d->adc_bits) - 1u;
    const uint64_t pin_uv = ((uint64_t)millivolts * 1000 * d->r_bottom_ohms) /
                            (d->r_top_ohms + d->r_bottom_ohms);
    const uint64_t counts = (pin_uv * full) / ((uint64_t)d->vref_mv * 1000);
    return (uint32_t)(counts > full ? full : counts);
}

uint32_t vbus_full_scale_mv(const vbus_divider_t *d) {
    if (d == NULL || d->r_bottom_ohms == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)d->vref_mv *
                       (d->r_top_ohms + d->r_bottom_ohms)) /
                      d->r_bottom_ohms);
}

uint32_t vbus_source_impedance_ohms(const vbus_divider_t *d) {
    if (d == NULL) {
        return 0;
    }
    const uint64_t sum = (uint64_t)d->r_top_ohms + d->r_bottom_ohms;
    if (sum == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)d->r_top_ohms * d->r_bottom_ohms) / sum);
}

uint32_t vbus_min_sampling_ns(uint32_t source_impedance_ohms,
                              uint32_t adc_input_r_ohms, uint32_t adc_cap_pf,
                              uint8_t adc_bits) {
    /* ln(2^(n+2)) = (n + 2) * ln 2, and ln 2 ~ 693/1000. */
    const uint64_t r = (uint64_t)source_impedance_ohms + adc_input_r_ohms;
    const uint64_t ln_term = ((uint64_t)adc_bits + 2u) * 693u;
    /* ohms * pF = picoseconds; divide by 1000 for ns, and by 1000 for the
     * ln_term's scaling. */
    return (uint32_t)((r * adc_cap_pf * ln_term) / 1000000u);
}

bool vbus_filter_cap_adequate(uint32_t filter_cap_nf, uint32_t adc_cap_pf) {
    if (adc_cap_pf == 0) {
        return true;
    }
    /* A filter cap 100x the sample-and-hold capacitor keeps the droop under
     * 1% of an LSB at 12 bits, so the divider's impedance stops mattering. */
    return (uint64_t)filter_cap_nf * 1000u >= (uint64_t)adc_cap_pf * 100u;
}

vbus_status_t vbus_classify(uint32_t measured_mv, uint32_t expected_mv,
                            uint32_t tolerance_permil) {
    if (measured_mv < 3000) {
        return VBUS_ABSENT; /* below anything a source would present */
    }
    if (measured_mv > PD_ABSOLUTE_MAX_MV) {
        /* Should be unreachable: the firmware never requests EPR, so 20 V is
         * the ceiling.  If this fires, either the invariant was broken or the
         * source is misbehaving -- either way, stop drawing power. */
        return VBUS_OVER_SPR;
    }
    if (expected_mv == 0) {
        return VBUS_OK;
    }

    const uint64_t band = ((uint64_t)expected_mv * tolerance_permil) / 1000u;
    if (measured_mv + band < expected_mv) {
        return VBUS_LOW;
    }
    if (measured_mv > expected_mv + band) {
        return VBUS_HIGH;
    }
    return VBUS_OK;
}

const char *vbus_status_str(vbus_status_t s) {
    switch (s) {
        case VBUS_ABSENT: return "absent";
        case VBUS_OK: return "ok";
        case VBUS_LOW: return "low";
        case VBUS_HIGH: return "high";
        case VBUS_OVER_SPR: return "OVER 21 V -- SPR invariant violated";
        default: return "unknown";
    }
}
