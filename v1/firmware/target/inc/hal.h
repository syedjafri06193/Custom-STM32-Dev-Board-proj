/* The thin target layer the bring-up application talks to.
 *
 * Everything here is a port: it moves bytes and toggles pins.  No policy
 * lives below this line -- what voltage to ask a source for, what bit timing
 * to program, what a 3.5 mA loop current means, are all decided in core/,
 * where they are covered by host tests.  When a decision starts creeping into
 * a driver, that is the signal it belongs in core/ instead.
 */

#ifndef TARGET_HAL_H
#define TARGET_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ads1220.h"
#include "can_timing.h"
#include "pd_policy.h"
#include "vbus.h"

/* ------------------------------------------------------------------ timing */

void delay_init(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);
uint32_t millis(void);

/* -------------------------------------------------------------------- gpio */

typedef enum {
    LED_USER,
    LED_CAN,
    LED_FAULT,
} led_t;

void gpio_init(void);
void led_set(led_t led, bool on);
void led_toggle(led_t led);

/* Section 6.2: the TCPP01 holds dead-battery Rd while the MCU is dark.  This
 * is the handover, and it must not happen before UCPD is driving CC. */
void tcpp01_release_dead_battery(void);

/* Section 6.3: the bulk capacitance stays off VBUS until a contract exists. */
void bulk_switch_enable(bool on);
bool bulk_switch_is_enabled(void);

/* Section 8.4 / 16: powering the island from the converter versus from the
 * bench jumper is the measurement, so it is a firmware control. */
void iso_power_enable(bool on);

void can_transceiver_standby(bool standby);

/* ----------------------------------------------------------------- console */

void console_init(uint32_t baud);
void console_putc(char c);
void console_write(const char *s);
int console_getc_timeout(uint32_t ms); /* -1 on timeout */

/* ---------------------------------------------------------------- iso SPI */

/* Wires SPI1 + CS to the transport function pointers ads1220.c expects, so
 * the same driver code the host tests exercise against a fake runs unchanged
 * against the real part across the isolator. */
void iso_spi_init(void);
void iso_spi_bind(ads1220_t *dev);
bool iso_adc_data_ready(void);

/* ------------------------------------------------------------- VBUS sense */

void vbus_adc_init(void);
uint32_t vbus_adc_read_counts(void);
uint32_t vbus_read_mv(void);

/* ------------------------------------------------------------------ FDCAN */

typedef struct {
    uint32_t id;
    uint8_t dlc;
    bool fd;
    bool brs;
    bool extended;
    uint8_t data[64];
} can_frame_t;

typedef enum {
    CAN_MODE_NORMAL,
    CAN_MODE_INTERNAL_LOOPBACK, /* bring-up step 17: no transceiver */
    CAN_MODE_EXTERNAL_LOOPBACK, /* step 18: through the transceiver */
    CAN_MODE_LISTEN_ONLY,
} can_mode_t;

int can_init(const can_timing_t *nominal, const can_timing_t *data,
             can_mode_t mode);
int can_send(const can_frame_t *f);
int can_recv(can_frame_t *f); /* 0 = got one, -1 = nothing pending */
void can_error_counters(uint8_t *tx_errors, uint8_t *rx_errors,
                        bool *bus_off);
uint8_t can_last_error_code(void);

/* ------------------------------------------------------------------- UCPD */

typedef enum {
    CC_NONE,
    CC_1,
    CC_2,
} cc_line_t;

void ucpd_init(void);
/* Which CC line the cable landed on, and the Rp advertisement seen on it.
 * Orientation detection is the part of PD that works without a protocol
 * stack, and it is what bring-up step 13 needs. */
cc_line_t ucpd_attached_line(void);
uint8_t ucpd_vstate(cc_line_t line);
bool ucpd_source_attached(void);

#endif /* TARGET_HAL_H */
