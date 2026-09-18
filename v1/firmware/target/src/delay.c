/* SysTick: a millisecond tick for timeouts, and a spin for microseconds.
 *
 * Deliberately no interrupt for the microsecond path.  The only places that
 * need sub-millisecond timing here are the ADS1220's t_CSSC settling and the
 * TCPP01 handover, and both are short enough that spinning is cheaper and far
 * more predictable than an ISR. */

#include "board.h"
#include "hal.h"
#include "rcc.h"
#include "stm32g474.h"

static volatile uint32_t s_millis;

void SysTick_Handler(void);
void SysTick_Handler(void) { s_millis++; }

void delay_init(void) {
    SysTick->RVR = (g_sysclk_hz / 1000u) - 1u;
    SysTick->CVR = 0;
    SysTick->CSR = SysTick_CSR_CLKSOURCE | SysTick_CSR_TICKINT |
                   SysTick_CSR_ENABLE;
}

uint32_t millis(void) { return s_millis; }

void delay_ms(uint32_t ms) {
    const uint32_t start = s_millis;
    /* Unsigned subtraction, so this stays correct across the 49-day wrap
     * instead of hanging for a month. */
    while ((s_millis - start) < ms) {
        __wfi();
    }
}

void delay_us(uint32_t us) {
    /* Four cycles per loop iteration on an M4 with the loop in flash behind
     * the ART accelerator; rounded conservatively so this never under-delays.
     * Under-delaying a chip-select setup time produces a part that answers
     * sometimes, which is the worst bug shape there is. */
    const uint32_t cycles_per_us = g_sysclk_hz / 1000000u;
    uint32_t loops = (us * cycles_per_us) / 4u;
    while (loops--) {
        __nop();
    }
}
