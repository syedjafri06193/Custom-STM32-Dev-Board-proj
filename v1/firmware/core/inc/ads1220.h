/* ADS1220 24-bit delta-sigma ADC driver (design.md sections 4.2, 8.2).
 *
 * One chip is the entire isolated measurement side: 4-channel mux, PGA,
 * 2.048 V internal reference, and two IDACs that leave RTD support as a
 * firmware-and-a-few-resistors change rather than a redesign.
 *
 * The transport is deliberately a pair of function pointers.  On the board the
 * SPI runs across an ADuM4154 isolator; on the test bench it is a fake that
 * replays register contents.  The driver cannot tell the difference, which is
 * how the register encoding and the conversion maths get tested without
 * hardware.
 */

#ifndef ADS1220_H
#define ADS1220_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Commands (datasheet table 15). */
#define ADS1220_CMD_RESET 0x06
#define ADS1220_CMD_START 0x08
#define ADS1220_CMD_POWERDOWN 0x02
#define ADS1220_CMD_RDATA 0x10
#define ADS1220_CMD_RREG 0x20 /* | (reg << 2) | (len - 1) */
#define ADS1220_CMD_WREG 0x40

#define ADS1220_INTERNAL_VREF_MV 2048
#define ADS1220_FULL_SCALE_COUNTS 8388607L /* 2^23 - 1 */

typedef enum {
    ADS1220_MUX_AIN0_AIN1 = 0x0,
    ADS1220_MUX_AIN0_AIN2 = 0x1,
    ADS1220_MUX_AIN0_AIN3 = 0x2,
    ADS1220_MUX_AIN1_AIN2 = 0x3,
    ADS1220_MUX_AIN1_AIN3 = 0x4,
    ADS1220_MUX_AIN2_AIN3 = 0x5,
    ADS1220_MUX_AIN1_AIN0 = 0x6,
    ADS1220_MUX_AIN3_AIN2 = 0x7,
    ADS1220_MUX_AIN0_AVSS = 0x8,
    ADS1220_MUX_AIN1_AVSS = 0x9,
    ADS1220_MUX_AIN2_AVSS = 0xA,
    ADS1220_MUX_AIN3_AVSS = 0xB,
    ADS1220_MUX_VREF_DIV4 = 0xC,
    ADS1220_MUX_AVDD_DIV4 = 0xD,
    ADS1220_MUX_SHORTED = 0xE, /* both inputs to (AVDD+AVSS)/2: noise floor */
} ads1220_mux_t;

typedef enum {
    ADS1220_GAIN_1 = 0,
    ADS1220_GAIN_2 = 1,
    ADS1220_GAIN_4 = 2,
    ADS1220_GAIN_8 = 3,
    ADS1220_GAIN_16 = 4,
    ADS1220_GAIN_32 = 5,
    ADS1220_GAIN_64 = 6,
    ADS1220_GAIN_128 = 7,
} ads1220_gain_t;

typedef enum {
    ADS1220_DR_20SPS = 0, /* with the internal filter, excellent 50/60 Hz rejection */
    ADS1220_DR_45SPS = 1,
    ADS1220_DR_90SPS = 2,
    ADS1220_DR_175SPS = 3,
    ADS1220_DR_330SPS = 4,
    ADS1220_DR_600SPS = 5,
    ADS1220_DR_1000SPS = 6,
} ads1220_rate_t;

typedef enum {
    ADS1220_VREF_INTERNAL = 0,
    ADS1220_VREF_EXT_REFP0 = 1,
    ADS1220_VREF_EXT_REFP1 = 2,
    ADS1220_VREF_ANALOG_SUPPLY = 3,
} ads1220_vref_t;

typedef enum {
    ADS1220_FILTER_NONE = 0,
    ADS1220_FILTER_50_60HZ = 1, /* only meaningful at 20 SPS */
    ADS1220_FILTER_50HZ = 2,
    ADS1220_FILTER_60HZ = 3,
} ads1220_filter_t;

typedef struct {
    ads1220_mux_t mux;
    ads1220_gain_t gain;
    bool pga_bypass;
    ads1220_rate_t rate;
    bool continuous;
    bool temperature_sensor;
    bool burnout_current;
    ads1220_vref_t vref;
    ads1220_filter_t filter;
    uint16_t external_vref_mv; /* used when vref != internal */
} ads1220_config_t;

/* Transport: returns 0 on success.  `rx` may be NULL for write-only traffic. */
typedef int (*ads1220_xfer_fn)(void *ctx, const uint8_t *tx, uint8_t *rx,
                               size_t len);
typedef void (*ads1220_cs_fn)(void *ctx, bool asserted);

typedef struct {
    ads1220_xfer_fn xfer;
    ads1220_cs_fn cs;
    void *ctx;
    ads1220_config_t config;
} ads1220_t;

/* Encode the four configuration registers.  Pure function: the main reason the
 * register maths is testable without a part attached. */
void ads1220_encode(const ads1220_config_t *cfg, uint8_t regs[4]);
void ads1220_decode(const uint8_t regs[4], ads1220_config_t *cfg);

int ads1220_init(ads1220_t *dev, const ads1220_config_t *cfg);
int ads1220_reset(ads1220_t *dev);
int ads1220_start(ads1220_t *dev);
int ads1220_write_config(ads1220_t *dev, const ads1220_config_t *cfg);
int ads1220_read_regs(ads1220_t *dev, uint8_t regs[4]);
int ads1220_read_raw(ads1220_t *dev, int32_t *code);
int ads1220_set_mux(ads1220_t *dev, ads1220_mux_t mux);

/* Sign-extend a 24-bit two's complement result into int32. */
int32_t ads1220_sign_extend(uint32_t raw24);

/* Conversion maths, in microvolts to avoid floating point on the target. */
int32_t ads1220_code_to_uv(int32_t code, uint16_t vref_mv, ads1220_gain_t gain);
int32_t ads1220_uv_to_code(int32_t microvolts, uint16_t vref_mv,
                           ads1220_gain_t gain);
uint8_t ads1220_gain_value(ads1220_gain_t gain);
uint16_t ads1220_rate_sps(ads1220_rate_t rate);

#endif /* ADS1220_H */
