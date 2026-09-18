/* ADC1 on the VBUS divider (section 6.6).
 *
 * The divider is 1 M / 143 k, which is microamps of quiescent draw and a
 * source impedance of about 125 k.  That impedance is the whole story here:
 * charging the ADC's 5 pF sample-and-hold through 125 k to 12-bit settling
 * needs more than 4 us, and the default sampling time is a fraction of that.
 * vbus.c computes the requirement; this file honours it by using the longest
 * sampling time the part offers, and the board backs it up with a filter cap
 * at the pin so the divider's impedance stops mattering at all.
 *
 * Getting this wrong does not produce an obviously broken reading.  It
 * produces a reading that is low by a few percent and drifts with the
 * previous channel's value, which is exactly the kind of error somebody
 * spends a day calibrating out instead of fixing.
 */

#include "board.h"
#include "hal.h"
#include "stm32g474.h"
#include "vbus.h"

#define SMP_640_5 7u /* SMPx = 111: 640.5 ADC clock cycles */

static vbus_divider_t s_divider;

/* SMPR1 holds channels 0-9, SMPR2 holds 10-18, three bits each. */
static void set_sampling_time(uint32_t channel, uint32_t smp) {
    if (channel <= 9u) {
        const uint32_t shift = channel * 3u;
        ADC1->SMPR1 = (ADC1->SMPR1 & ~(7u << shift)) | (smp << shift);
    } else if (channel <= 18u) {
        const uint32_t shift = (channel - 10u) * 3u;
        ADC1->SMPR2 = (ADC1->SMPR2 & ~(7u << shift)) | (smp << shift);
    }
}

void vbus_adc_init(void) {
    s_divider = vbus_default_divider();

    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
    (void)RCC->AHB2ENR;

    /* HCLK/4 = 42.5 MHz, inside the part's 60 MHz ADC limit, and synchronous
     * with the core so the sampling time computes from a known clock instead
     * of from the asynchronous kernel clock's own tolerance. */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~(3u << ADC_CCR_CKMODE_Pos)) |
                        (3u << ADC_CCR_CKMODE_Pos) | ADC_CCR_VREFEN;

    /* Leave deep power-down, then start the internal regulator and give it
     * the 20 us it needs.  Skipping the wait yields an ADC that never sets
     * ADRDY, which looks like a dead peripheral. */
    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;
    delay_us(25);

    /* Single-ended offset calibration.  This is worth doing on a board where
     * the thing being measured is a 20 V rail divided down by seven: every
     * LSB of ADC offset shows up as 35 mV of VBUS error. */
    ADC1->CR &= ~ADC_CR_ADCALDIF;
    ADC1->CR |= ADC_CR_ADCAL;
    while (ADC1->CR & ADC_CR_ADCAL) {
    }

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    while ((ADC1->ISR & ADC_ISR_ADRDY) == 0u) {
    }

    /* Longest sampling time on the VBUS channel -- see the header comment. */
    set_sampling_time(VBUS_ADC_CHANNEL, SMP_640_5);

    /* One conversion, one channel. */
    ADC1->SQR1 = (VBUS_ADC_CHANNEL << 6);
    ADC1->CFGR = 0; /* single conversion, right-aligned, no DMA */
}

uint32_t vbus_adc_read_counts(void) {
    ADC1->ISR = ADC_ISR_EOC;
    ADC1->CR |= ADC_CR_ADSTART;
    while ((ADC1->ISR & ADC_ISR_EOC) == 0u) {
    }
    return ADC1->DR & 0xFFFFu;
}

uint32_t vbus_read_mv(void) {
    /* Eight samples averaged.  Not for precision -- the divider tolerance
     * dominates -- but because a single sample taken during a PD voltage
     * transition is a number that never existed. */
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += vbus_adc_read_counts();
    }
    return vbus_mv_from_counts(&s_divider, sum / 8u);
}
