#ifndef TARGET_RCC_H
#define TARGET_RCC_H

#include <stdint.h>

enum {
    RCC_ERR_VOS = -1,
    RCC_ERR_FLASH_LATENCY = -2,
    RCC_ERR_HSE = -3,
    RCC_ERR_PLL = -4,
};

/* Brings the part to 170 MHz from the external crystal.  Returns 0, or one of
 * the RCC_ERR_* codes -- distinct codes because "no clock" during bring-up is
 * several different faults with several different fixes. */
int rcc_clock_init(void);

/* Drives SYSCLK/16 out on MCO for the crystal check in bring-up step 12. */
void rcc_mco_enable(void);
uint32_t rcc_mco_expected_hz(void);

extern uint32_t g_sysclk_hz;

#endif /* TARGET_RCC_H */
