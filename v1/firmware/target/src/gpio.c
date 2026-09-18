/* Pin configuration, LEDs, and the three power-path controls that are
 * firmware decisions rather than hardware ones. */

#include "gpio.h"

#include "board.h"
#include "hal.h"
#include "stm32g474.h"

#define MODE_INPUT GPIO_MODE_INPUT
#define MODE_OUTPUT GPIO_MODE_OUTPUT
#define MODE_AF GPIO_MODE_AF
#define MODE_ANALOG GPIO_MODE_ANALOG

void gpio_pin_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode) {
    port->MODER = (port->MODER & ~(3u << (pin * 2))) | (mode << (pin * 2));
}

void gpio_pin_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af) {
    const uint32_t idx = pin >> 3;
    const uint32_t shift = (pin & 7u) * 4u;
    port->AFR[idx] = (port->AFR[idx] & ~(0xFu << shift)) | (af << shift);
    gpio_pin_mode(port, pin, MODE_AF);
}

void gpio_pin_pull(GPIO_TypeDef *port, uint32_t pin, uint32_t pull) {
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2))) | (pull << (pin * 2));
}

void gpio_pin_speed(GPIO_TypeDef *port, uint32_t pin, uint32_t speed) {
    port->OSPEEDR = (port->OSPEEDR & ~(3u << (pin * 2))) | (speed << (pin * 2));
}

void gpio_pin_write(GPIO_TypeDef *port, uint32_t pin, bool high) {
    port->BSRR = high ? (1u << pin) : (1u << (pin + 16));
}

bool gpio_pin_read(GPIO_TypeDef *port, uint32_t pin) {
    return (port->IDR & (1u << pin)) != 0u;
}

static bool s_bulk_enabled;

void gpio_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN |
                    RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIOFEN;
    (void)RCC->AHB2ENR;

    /* --- power path, configured to the safe state before anything else ---
     *
     * Order matters here and it is the order of section 6.  The bulk switch
     * must be off before VBUS can rise, because a source that sees 470 uF of
     * bypass capacitance before a contract exists may declare a fault; and
     * the TCPP01 must keep presenting dead-battery Rd until UCPD is up, or
     * the source stops sourcing and the board browns out mid-handover. */
    gpio_pin_write(PIN_BULK_EN_PORT, PIN_BULK_EN, false);
    gpio_pin_mode(PIN_BULK_EN_PORT, PIN_BULK_EN, MODE_OUTPUT);
    s_bulk_enabled = false;

    gpio_pin_write(PIN_TCPP_DBN_PORT, PIN_TCPP_DBN, false); /* dead battery held */
    gpio_pin_mode(PIN_TCPP_DBN_PORT, PIN_TCPP_DBN, MODE_OUTPUT);

    gpio_pin_write(PIN_ISO_PWR_EN_PORT, PIN_ISO_PWR_EN, false);
    gpio_pin_mode(PIN_ISO_PWR_EN_PORT, PIN_ISO_PWR_EN, MODE_OUTPUT);

    /* The transceiver comes up in standby so the board cannot put anything on
     * a live bus before the bit timing has been programmed.  A node that
     * transmits at the wrong bit rate does not merely fail; it corrupts every
     * other node's traffic while it does so. */
    gpio_pin_write(PIN_CAN_STB_PORT, PIN_CAN_STB, true);
    gpio_pin_mode(PIN_CAN_STB_PORT, PIN_CAN_STB, MODE_OUTPUT);

    /* --- LEDs --- */
    gpio_pin_write(PIN_LED_USER_PORT, PIN_LED_USER, false);
    gpio_pin_mode(PIN_LED_USER_PORT, PIN_LED_USER, MODE_OUTPUT);
    gpio_pin_write(PIN_LED_CAN_PORT, PIN_LED_CAN, false);
    gpio_pin_mode(PIN_LED_CAN_PORT, PIN_LED_CAN, MODE_OUTPUT);
    gpio_pin_write(PIN_LED_FAULT_PORT, PIN_LED_FAULT, false);
    gpio_pin_mode(PIN_LED_FAULT_PORT, PIN_LED_FAULT, MODE_OUTPUT);

    /* --- ADS1220 chip select and data-ready --- */
    gpio_pin_write(PIN_ADC_CS_PORT, PIN_ADC_CS, true); /* idle high */
    gpio_pin_mode(PIN_ADC_CS_PORT, PIN_ADC_CS, MODE_OUTPUT);
    gpio_pin_speed(PIN_ADC_CS_PORT, PIN_ADC_CS, 2u);

    gpio_pin_mode(PIN_ADC_DRDY_PORT, PIN_ADC_DRDY, MODE_INPUT);
    gpio_pin_pull(PIN_ADC_DRDY_PORT, PIN_ADC_DRDY, 1u); /* pull-up: /DRDY */

    /* --- CAN pins.  Configured but the transceiver is still in standby. --- */
    gpio_pin_speed(PIN_CAN_TX_PORT, PIN_CAN_TX, 2u);
    gpio_pin_af(PIN_CAN_TX_PORT, PIN_CAN_TX, PIN_CAN_AF);
    gpio_pin_af(PIN_CAN_RX_PORT, PIN_CAN_RX, PIN_CAN_AF);

    /* --- CC lines are analog; the UCPD peripheral owns the pads. --- */
    gpio_pin_mode(PIN_CC1_PORT, PIN_CC1, MODE_ANALOG);
    gpio_pin_pull(PIN_CC1_PORT, PIN_CC1, 0u);
    gpio_pin_mode(PIN_CC2_PORT, PIN_CC2, MODE_ANALOG);
    gpio_pin_pull(PIN_CC2_PORT, PIN_CC2, 0u);

    /* --- VBUS sense --- */
    gpio_pin_mode(PIN_VBUS_SENSE_PORT, PIN_VBUS_SENSE, MODE_ANALOG);
}

void led_set(led_t led, bool on) {
    switch (led) {
        case LED_USER: gpio_pin_write(PIN_LED_USER_PORT, PIN_LED_USER, on); break;
        case LED_CAN: gpio_pin_write(PIN_LED_CAN_PORT, PIN_LED_CAN, on); break;
        case LED_FAULT: gpio_pin_write(PIN_LED_FAULT_PORT, PIN_LED_FAULT, on); break;
        default: break;
    }
}

void led_toggle(led_t led) {
    switch (led) {
        case LED_USER: PIN_LED_USER_PORT->ODR ^= (1u << PIN_LED_USER); break;
        case LED_CAN: PIN_LED_CAN_PORT->ODR ^= (1u << PIN_LED_CAN); break;
        case LED_FAULT: PIN_LED_FAULT_PORT->ODR ^= (1u << PIN_LED_FAULT); break;
        default: break;
    }
}

void tcpp01_release_dead_battery(void) {
    /* Two things have to happen, in this order, and both are section 6.2.
     *
     * PWR_CR3's UCPD1_DBDIS releases the STM32's own internal dead-battery
     * pull-downs on the CC pads.  DBn on the TCPP01 releases the external
     * ones.  Doing either before UCPD is enabled and driving the CC lines
     * leaves the port momentarily presenting nothing, the source removes
     * VBUS, and a board with no battery loses the rail it is running on --
     * which is the bricking trap, and it is not recoverable without lifting
     * the part or shorting CC by hand. */
    PWR->CR3 |= PWR_CR3_UCPD1_DBDIS;
    gpio_pin_write(PIN_TCPP_DBN_PORT, PIN_TCPP_DBN, true);
}

void bulk_switch_enable(bool on) {
    gpio_pin_write(PIN_BULK_EN_PORT, PIN_BULK_EN, on);
    s_bulk_enabled = on;
}

bool bulk_switch_is_enabled(void) { return s_bulk_enabled; }

void iso_power_enable(bool on) {
    gpio_pin_write(PIN_ISO_PWR_EN_PORT, PIN_ISO_PWR_EN, on);
}

void can_transceiver_standby(bool standby) {
    gpio_pin_write(PIN_CAN_STB_PORT, PIN_CAN_STB, standby);
}
