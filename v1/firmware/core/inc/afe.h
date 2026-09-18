/* Analog front end scaling and fault detection (design.md sections 8.1, 8.2).
 *
 * 4-20 mA through a 100 ohm 0.1% shunt gives 0.4-2.0 V, which sits inside the
 * ADS1220's 2.048 V internal reference at PGA = 1 with headroom.  That shunt
 * *is* the measurement accuracy, which is why its tolerance and tempco are
 * part of the error budget below rather than an afterthought.
 *
 * The fault thresholds are NAMUR NE43, the convention every industrial
 * transmitter follows: a live sensor never sits below 3.6 mA or above 21 mA,
 * so those regions carry diagnostic meaning.  Under 3.6 mA usually means a
 * broken wire; over 21 mA usually means a short.  Reporting "0.0 units" for a
 * cut cable is how a logger lies to you.
 */

#ifndef AFE_H
#define AFE_H

#include <stdbool.h>
#include <stdint.h>

#define AFE_CHANNELS 4

/* NAMUR NE43 namur-conformant live-zero limits, in microamps. */
#define AFE_NAMUR_LOW_UA 3600
#define AFE_NAMUR_HIGH_UA 21000
#define AFE_LOOP_MIN_UA 4000
#define AFE_LOOP_MAX_UA 20000

typedef enum {
    AFE_INPUT_4_20MA = 0,
    AFE_INPUT_0_10V = 1,
} afe_input_type_t;

typedef enum {
    AFE_OK = 0,
    AFE_UNDERRANGE,  /* below 4 mA but inside the live-zero band */
    AFE_OVERRANGE,   /* above 20 mA but inside the band */
    AFE_FAULT_LOW,   /* below NE43 low: broken wire, dead transmitter */
    AFE_FAULT_HIGH,  /* above NE43 high: short, or the loop supply on the input */
    AFE_SATURATED,   /* the ADC itself is at the rail */
} afe_status_t;

typedef struct {
    afe_input_type_t type;
    uint32_t shunt_milliohms;  /* 100 ohm shunt = 100000 */
    uint32_t divider_num;      /* 0-10 V input: attenuation numerator */
    uint32_t divider_den;      /* and denominator */
    int32_t offset_uv;         /* measured at calibration, subtracted */
    int32_t gain_ppm;          /* measured gain error, corrected out */
    /* Engineering units mapped onto 4-20 mA, in millis (so 0..100.000 %). */
    int32_t eu_at_min_milli;
    int32_t eu_at_max_milli;
} afe_channel_cfg_t;

typedef struct {
    int32_t microvolts;   /* after offset and gain correction */
    int32_t microamps;    /* current-loop channels only */
    int32_t eu_milli;     /* engineering units, in millis */
    int32_t percent_milli;/* 0-100% of span, in millis */
    afe_status_t status;
} afe_reading_t;

/* Default 4-20 mA channel: 100 ohm shunt, 0-100% span, no calibration yet. */
afe_channel_cfg_t afe_default_4_20ma(void);
afe_channel_cfg_t afe_default_0_10v(void);

/* Convert a corrected ADC voltage into a reading. */
afe_reading_t afe_convert(const afe_channel_cfg_t *cfg, int32_t raw_microvolts);

/* Shunt voltage a given loop current produces -- used by the self-test to
 * check a channel against an injected current. */
int32_t afe_current_to_uv(const afe_channel_cfg_t *cfg, int32_t microamps);

/* Noise-free bits from an RMS noise figure, the section 16 metric:
 *
 *     noise_free_bits = log2(full_scale_range / (6.6 * rms_noise))
 *
 * Returned in millibits (18500 = 18.5 bits) to stay in integer arithmetic on
 * the target.  The 6.6 is the usual peak-to-peak/RMS factor for Gaussian
 * noise; it is the convention TI's own datasheets use, so the number here is
 * comparable with the datasheet's.
 */
int32_t afe_noise_free_millibits(int32_t full_scale_uv, int32_t rms_noise_uv);

/* Worst-case error budget for a channel, in parts per million of span:
 * shunt tolerance and tempco, reference error, and ADC gain error, added as a
 * worst case rather than RSS because the point of the number is the bound. */
int32_t afe_error_budget_ppm(uint32_t shunt_tolerance_ppm,
                             uint32_t shunt_tempco_ppm_per_c,
                             int32_t delta_temp_c, uint32_t vref_tolerance_ppm,
                             uint32_t adc_gain_error_ppm);

const char *afe_status_str(afe_status_t s);

#endif /* AFE_H */
