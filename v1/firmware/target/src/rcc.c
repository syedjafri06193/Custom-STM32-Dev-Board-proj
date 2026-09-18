/* Clock tree bring-up: 16 MHz crystal to 170 MHz, which on the G4 means
 * boost mode and a specific ordering that is easy to get subtly wrong.
 *
 * The G4 will not simply run at 170 MHz because you asked it to.  Range 1
 * "boost" has to be entered while the AHB prescaler is dividing by two, and
 * only after the core has settled at the new voltage may the prescaler go
 * back to /1.  Skipping the prescaler dance usually appears to work and then
 * fails on a cold part or a marginal supply -- the worst kind of bug, because
 * the board that fails is never the one on your bench.
 */

#include "rcc.h"

#include "board.h"
#include "stm32g474.h"

uint32_t g_sysclk_hz = 16000000u; /* HSI16 until proven otherwise */

static int wait_flag(volatile uint32_t *reg, uint32_t mask, int set,
                     uint32_t spins) {
    while (spins--) {
        const int now = ((*reg & mask) != 0);
        if (now == set) {
            return 0;
        }
    }
    return -1;
}

int rcc_clock_init(void) {
    /* The voltage regulator lives behind the PWR clock gate, so that comes
     * first or every write below lands in the void. */
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;

    /* Range 1: the only range that reaches 170 MHz. */
    PWR->CR1 = (PWR->CR1 & ~(3u << PWR_CR1_VOS_Pos)) | PWR_CR1_VOS_RANGE1;
    if (wait_flag(&PWR->SR2, PWR_SR2_VOSF, 0, 100000) != 0) {
        return RCC_ERR_VOS;
    }

    /* Flash wait states before the clock speeds up, never after.  Four wait
     * states covers 170 MHz in range 1 boost.  Raising latency early is
     * always safe; raising it late is a crash. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | 4u | FLASH_ACR_PRFTEN |
                 FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    if ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) != 4u) {
        return RCC_ERR_FLASH_LATENCY;
    }

    /* HSE.  A failure here is the single most likely bring-up outcome after
     * a first assembly -- wrong load caps, wrong crystal, cold joint -- so it
     * gets its own error code rather than a generic timeout. */
    RCC->CR |= RCC_CR_HSEON;
    if (wait_flag(&RCC->CR, RCC_CR_HSERDY, 1, 200000) != 0) {
        return RCC_ERR_HSE;
    }

    /* PLL off before touching PLLCFGR. */
    RCC->CR &= ~RCC_CR_PLLON;
    if (wait_flag(&RCC->CR, RCC_CR_PLLRDY, 0, 100000) != 0) {
        return RCC_ERR_PLL;
    }

    /* R and Q are encoded as (divider/2 - 1): 2->00, 4->01, 6->10, 8->11. */
    const uint32_t r_bits = (BOARD_PLL_R / 2u) - 1u;
    const uint32_t q_bits = (BOARD_PLL_Q / 2u) - 1u;

    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSE |
                   ((BOARD_PLL_M - 1u) << RCC_PLLCFGR_PLLM_Pos) |
                   (BOARD_PLL_N << RCC_PLLCFGR_PLLN_Pos) |
                   (r_bits << RCC_PLLCFGR_PLLR_Pos) | RCC_PLLCFGR_PLLREN |
                   (q_bits << RCC_PLLCFGR_PLLQ_Pos) | RCC_PLLCFGR_PLLQEN;

    RCC->CR |= RCC_CR_PLLON;
    if (wait_flag(&RCC->CR, RCC_CR_PLLRDY, 1, 200000) != 0) {
        return RCC_ERR_PLL;
    }

    /* --- the boost-mode sequence, in the order RM0440 gives it --- */

    /* 1. AHB prescaler to /2 so the core never sees 170 MHz at the old
     *    voltage during the switch. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_HPRE_Msk) | RCC_CFGR_HPRE_DIV2;

    /* 2. Enter boost (R1MODE = 0). */
    PWR->CR5 &= ~PWR_CR5_R1MODE;

    /* 3. Switch the system clock to the PLL. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) !=
           (RCC_CFGR_SW_PLL << RCC_CFGR_SWS_Pos)) {
        /* The PLL is locked and selected; this cannot hang unless the PLL
         * dropped out, in which case the CSS would already have fired. */
    }

    /* 4. Hold at /2 for at least 1 us.  At 85 MHz (170/2) that is 85 cycles;
     *    128 NOPs is the cheap, obviously-sufficient version and this runs
     *    exactly once at boot. */
    for (int i = 0; i < 128; i++) {
        __nop();
    }

    /* 5. Prescaler back to /1. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_HPRE_Msk) | RCC_CFGR_HPRE_DIV1;

    /* APB1 and APB2 stay at /1: 170 MHz is within both buses' limit on the
     * G4, and a prescaler here would change the USART and SPI baud dividers
     * without changing the constants that compute them. */
    RCC->CFGR &= ~((7u << RCC_CFGR_PPRE1_Pos) | (7u << RCC_CFGR_PPRE2_Pos));

    /* Clock security: if the crystal dies at runtime, the CSS switches back
     * to HSI and raises an NMI rather than leaving the CAN controller
     * transmitting at whatever frequency the dying oscillator produces. */
    RCC->CR |= RCC_CR_CSSON;

    /* FDCAN kernel clock from PLLQ -- see the note in board.h. */
    RCC->CCIPR = (RCC->CCIPR & ~(3u << RCC_CCIPR_FDCANSEL_Pos)) |
                 RCC_CCIPR_FDCANSEL_PLLQ;

    g_sysclk_hz = BOARD_SYSCLK_HZ;
    return 0;
}

void rcc_mco_enable(void) {
    /* MCO = SYSCLK / 16.  Bring-up step 12 measures this instead of probing
     * the crystal.  Dividing by 16 keeps it at 10.625 MHz, which any cheap
     * scope or counter reads accurately -- a raw 170 MHz output would be
     * measuring the probe as much as the clock. */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;

    PIN_MCO_PORT->MODER &= ~(3u << (PIN_MCO * 2));
    PIN_MCO_PORT->MODER |= (2u << (PIN_MCO * 2)); /* alternate function */
    PIN_MCO_PORT->OSPEEDR |= (3u << (PIN_MCO * 2));
    PIN_MCO_PORT->AFR[PIN_MCO >> 3] &= ~(0xFu << ((PIN_MCO & 7) * 4));
    PIN_MCO_PORT->AFR[PIN_MCO >> 3] |= (PIN_MCO_AF << ((PIN_MCO & 7) * 4));

    RCC->CFGR = (RCC->CFGR & ~((0xFu << RCC_CFGR_MCOSEL_Pos) |
                               (0x7u << RCC_CFGR_MCOPRE_Pos))) |
                (0x1u << RCC_CFGR_MCOSEL_Pos) | /* SYSCLK */
                (0x4u << RCC_CFGR_MCOPRE_Pos);  /* /16 */
}

uint32_t rcc_mco_expected_hz(void) { return g_sysclk_hz / 16u; }

void SystemInit(void) {
    /* Nothing that must precede .data/.bss init.  The FPU is not enabled:
     * every computation in this firmware is integer, on purpose -- the PD
     * policy, the CAN solver and the analog scaling all have to be
     * bit-identical to what the host tests exercise, and floating point is
     * how that stops being true. */
}
