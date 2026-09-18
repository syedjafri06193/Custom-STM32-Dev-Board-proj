/* VBUS monitoring (design.md section 6.6).
 *
 * "A debugging signal during bring-up worth more than any amount of printf" --
 * it confirms the negotiated voltage actually appeared, catches a source that
 * dropped out, and is the only way to know whether a PD contract did what the
 * analyzer said it did.
 *
 * The divider has to be high-impedance so it does not waste microamps at 20 V,
 * and that makes the source impedance a real constraint on the ADC's sampling
 * time.  `vbus_min_sampling_ns()` computes the requirement instead of leaving
 * it to be discovered as a reading that is mysteriously 3% low.
 */

#ifndef VBUS_H
#define VBUS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VBUS_ABSENT = 0,  /* no source, or it dropped out */
    VBUS_OK,          /* within tolerance of the expected contract */
    VBUS_LOW,         /* present but below expectation: browning out? */
    VBUS_HIGH,        /* above expectation but inside the rating */
    VBUS_OVER_SPR,    /* above 21 V: should be impossible; see section 6.1 */
} vbus_status_t;

typedef struct {
    uint32_t r_top_ohms;    /* divider top leg */
    uint32_t r_bottom_ohms; /* divider bottom leg, the tapped one */
    uint16_t vref_mv;       /* ADC reference */
    uint8_t adc_bits;
} vbus_divider_t;

/* 1 M / 143 k: 24 V maps to 3.00 V, and at 20 V the divider draws 17 uA. */
vbus_divider_t vbus_default_divider(void);

uint32_t vbus_mv_from_counts(const vbus_divider_t *d, uint32_t counts);
uint32_t vbus_counts_from_mv(const vbus_divider_t *d, uint32_t millivolts);

/* Full-scale VBUS the divider can represent before the ADC clips. */
uint32_t vbus_full_scale_mv(const vbus_divider_t *d);

/* Thevenin source impedance the ADC sees. */
uint32_t vbus_source_impedance_ohms(const vbus_divider_t *d);

/* Minimum ADC sampling time for the converter to settle to within half an LSB:
 *
 *     t >= (R_src + R_adc) * C_adc * ln(2^(n+2))
 *
 * With a megohm-class divider this comes out in microseconds, which is far
 * longer than the default sampling time -- hence the filter capacitor at the
 * pin, which is what makes the requirement satisfiable.
 */
uint32_t vbus_min_sampling_ns(uint32_t source_impedance_ohms,
                              uint32_t adc_input_r_ohms, uint32_t adc_cap_pf,
                              uint8_t adc_bits);

/* Does a filter capacitor at the pin make the sampling requirement moot?  A
 * cap that is large relative to the ADC's sample-and-hold capacitor supplies
 * the charge locally, so the divider only has to recharge it between samples. */
bool vbus_filter_cap_adequate(uint32_t filter_cap_nf, uint32_t adc_cap_pf);

vbus_status_t vbus_classify(uint32_t measured_mv, uint32_t expected_mv,
                            uint32_t tolerance_permil);

const char *vbus_status_str(vbus_status_t s);

#endif /* VBUS_H */
