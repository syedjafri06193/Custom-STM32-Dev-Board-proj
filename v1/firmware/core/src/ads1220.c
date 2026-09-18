/* See ads1220.h. */

#include "ads1220.h"

#include <string.h>

uint8_t ads1220_gain_value(ads1220_gain_t gain) {
    return (uint8_t)(1u << (unsigned)gain);
}

uint16_t ads1220_rate_sps(ads1220_rate_t rate) {
    static const uint16_t sps[] = {20, 45, 90, 175, 330, 600, 1000};
    if ((unsigned)rate >= sizeof(sps) / sizeof(sps[0])) {
        return 0;
    }
    return sps[rate];
}

void ads1220_encode(const ads1220_config_t *cfg, uint8_t regs[4]) {
    if (cfg == NULL || regs == NULL) {
        return;
    }
    memset(regs, 0, 4);

    /* Reg0: MUX[7:4] GAIN[3:1] PGA_BYPASS[0] */
    regs[0] = (uint8_t)(((cfg->mux & 0x0F) << 4) | ((cfg->gain & 0x07) << 1) |
                        (cfg->pga_bypass ? 1u : 0u));

    /* Reg1: DR[7:5] MODE[4:3] CM[2] TS[1] BCS[0] */
    regs[1] = (uint8_t)(((cfg->rate & 0x07) << 5) |
                        (cfg->continuous ? (1u << 2) : 0u) |
                        (cfg->temperature_sensor ? (1u << 1) : 0u) |
                        (cfg->burnout_current ? 1u : 0u));

    /* Reg2: VREF[7:6] 50/60[5:4] PSW[3] IDAC[2:0] */
    regs[2] = (uint8_t)(((cfg->vref & 0x03) << 6) | ((cfg->filter & 0x03) << 4));

    /* Reg3 stays zero: IDACs unrouted, DRDY on its own pin. */
    regs[3] = 0;
}

void ads1220_decode(const uint8_t regs[4], ads1220_config_t *cfg) {
    if (cfg == NULL || regs == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->mux = (ads1220_mux_t)((regs[0] >> 4) & 0x0F);
    cfg->gain = (ads1220_gain_t)((regs[0] >> 1) & 0x07);
    cfg->pga_bypass = (regs[0] & 0x01) != 0;
    cfg->rate = (ads1220_rate_t)((regs[1] >> 5) & 0x07);
    cfg->continuous = (regs[1] & (1u << 2)) != 0;
    cfg->temperature_sensor = (regs[1] & (1u << 1)) != 0;
    cfg->burnout_current = (regs[1] & 0x01) != 0;
    cfg->vref = (ads1220_vref_t)((regs[2] >> 6) & 0x03);
    cfg->filter = (ads1220_filter_t)((regs[2] >> 4) & 0x03);
}

int32_t ads1220_sign_extend(uint32_t raw24) {
    if (raw24 & 0x800000u) {
        return (int32_t)(raw24 | 0xFF000000u);
    }
    return (int32_t)(raw24 & 0x00FFFFFFu);
}

int32_t ads1220_code_to_uv(int32_t code, uint16_t vref_mv,
                           ads1220_gain_t gain) {
    /* Vin = code * VREF / (gain * 2^23), in microvolts, done in 64-bit so the
     * intermediate does not overflow. */
    const int64_t vref_uv = (int64_t)vref_mv * 1000;
    const int64_t denom = (int64_t)ads1220_gain_value(gain) * 8388608LL;
    return (int32_t)(((int64_t)code * vref_uv) / denom);
}

int32_t ads1220_uv_to_code(int32_t microvolts, uint16_t vref_mv,
                           ads1220_gain_t gain) {
    const int64_t vref_uv = (int64_t)vref_mv * 1000;
    if (vref_uv == 0) {
        return 0;
    }
    const int64_t num =
        (int64_t)microvolts * ads1220_gain_value(gain) * 8388608LL;
    return (int32_t)(num / vref_uv);
}

static int write_cmd(ads1220_t *dev, uint8_t cmd) {
    if (dev == NULL || dev->xfer == NULL) {
        return -1;
    }
    if (dev->cs) {
        dev->cs(dev->ctx, true);
    }
    const int rc = dev->xfer(dev->ctx, &cmd, NULL, 1);
    if (dev->cs) {
        dev->cs(dev->ctx, false);
    }
    return rc;
}

int ads1220_reset(ads1220_t *dev) {
    return write_cmd(dev, ADS1220_CMD_RESET);
}

int ads1220_start(ads1220_t *dev) {
    return write_cmd(dev, ADS1220_CMD_START);
}

int ads1220_write_config(ads1220_t *dev, const ads1220_config_t *cfg) {
    if (dev == NULL || dev->xfer == NULL || cfg == NULL) {
        return -1;
    }
    uint8_t tx[5];
    tx[0] = (uint8_t)(ADS1220_CMD_WREG | (0 << 2) | (4 - 1));
    ads1220_encode(cfg, &tx[1]);

    if (dev->cs) {
        dev->cs(dev->ctx, true);
    }
    const int rc = dev->xfer(dev->ctx, tx, NULL, sizeof(tx));
    if (dev->cs) {
        dev->cs(dev->ctx, false);
    }
    if (rc == 0) {
        dev->config = *cfg;
    }
    return rc;
}

int ads1220_read_regs(ads1220_t *dev, uint8_t regs[4]) {
    if (dev == NULL || dev->xfer == NULL || regs == NULL) {
        return -1;
    }
    uint8_t tx[5] = {(uint8_t)(ADS1220_CMD_RREG | (0 << 2) | (4 - 1)), 0, 0, 0, 0};
    uint8_t rx[5] = {0};

    if (dev->cs) {
        dev->cs(dev->ctx, true);
    }
    const int rc = dev->xfer(dev->ctx, tx, rx, sizeof(tx));
    if (dev->cs) {
        dev->cs(dev->ctx, false);
    }
    if (rc == 0) {
        memcpy(regs, &rx[1], 4);
    }
    return rc;
}

int ads1220_init(ads1220_t *dev, const ads1220_config_t *cfg) {
    if (dev == NULL || cfg == NULL) {
        return -1;
    }
    int rc = ads1220_reset(dev);
    if (rc != 0) {
        return rc;
    }
    rc = ads1220_write_config(dev, cfg);
    if (rc != 0) {
        return rc;
    }
    if (cfg->continuous) {
        rc = ads1220_start(dev);
    }
    return rc;
}

int ads1220_set_mux(ads1220_t *dev, ads1220_mux_t mux) {
    if (dev == NULL) {
        return -1;
    }
    ads1220_config_t cfg = dev->config;
    cfg.mux = mux;
    return ads1220_write_config(dev, &cfg);
}

int ads1220_read_raw(ads1220_t *dev, int32_t *code) {
    if (dev == NULL || dev->xfer == NULL || code == NULL) {
        return -1;
    }
    uint8_t tx[4] = {ADS1220_CMD_RDATA, 0, 0, 0};
    uint8_t rx[4] = {0};

    if (dev->cs) {
        dev->cs(dev->ctx, true);
    }
    const int rc = dev->xfer(dev->ctx, tx, rx, sizeof(tx));
    if (dev->cs) {
        dev->cs(dev->ctx, false);
    }
    if (rc != 0) {
        return rc;
    }

    const uint32_t raw =
        ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | (uint32_t)rx[3];
    *code = ads1220_sign_extend(raw);
    return 0;
}
