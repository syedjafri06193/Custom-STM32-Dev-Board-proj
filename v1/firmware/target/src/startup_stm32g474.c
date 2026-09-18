/* Reset vector, vector table, and the C runtime bring-up.
 *
 * Written in C rather than assembly so it is readable and so the fault
 * handlers can do something more useful than spin.  A board that hard-faults
 * during bring-up and gives you a blinking LED pattern plus the fault
 * registers over UART is a board you can debug; one that silently locks up is
 * not.
 */

#include <stdint.h>

#include "board.h"
#include "stm32g474.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

extern int main(void);
void SystemInit(void);
void Reset_Handler(void);
void Default_Handler(void);

/* The fault handlers latch what the core recorded before anything else can
 * overwrite it.  A debugger attached after the fact reads these. */
volatile uint32_t g_fault_cfsr;
volatile uint32_t g_fault_hfsr;
volatile uint32_t g_fault_mmfar;
volatile uint32_t g_fault_bfar;
volatile uint32_t g_fault_pc;

__attribute__((naked)) void HardFault_Handler(void);

static void fault_capture(uint32_t *frame) {
    g_fault_cfsr = SCB->CFSR;
    g_fault_hfsr = SCB->HFSR;
    g_fault_mmfar = SCB->MMFAR;
    g_fault_bfar = SCB->BFAR;
    g_fault_pc = frame[6]; /* stacked PC */

    /* Blink the fault LED forever.  Deliberately not a bare while(1): a board
     * that is visibly alive and unhappy is easier to diagnose than one that
     * looks dead. */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    PIN_LED_FAULT_PORT->MODER &= ~(3u << (PIN_LED_FAULT * 2));
    PIN_LED_FAULT_PORT->MODER |= (1u << (PIN_LED_FAULT * 2));
    for (;;) {
        PIN_LED_FAULT_PORT->ODR ^= (1u << PIN_LED_FAULT);
        for (volatile uint32_t i = 0; i < 400000; i++) {
            __nop();
        }
    }
}

__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4            \n"
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b fault_capture_trampoline\n");
}

void fault_capture_trampoline(uint32_t *frame);
void fault_capture_trampoline(uint32_t *frame) { fault_capture(frame); }

void Default_Handler(void) {
    for (;;) {
        __wfi();
    }
}

#define WEAK_ALIAS(name) \
    void name(void) __attribute__((weak, alias("Default_Handler")))

WEAK_ALIAS(NMI_Handler);
WEAK_ALIAS(MemManage_Handler);
WEAK_ALIAS(BusFault_Handler);
WEAK_ALIAS(UsageFault_Handler);
WEAK_ALIAS(SVC_Handler);
WEAK_ALIAS(DebugMon_Handler);
WEAK_ALIAS(PendSV_Handler);
WEAK_ALIAS(SysTick_Handler);
WEAK_ALIAS(FDCAN1_IT0_IRQHandler);
WEAK_ALIAS(FDCAN1_IT1_IRQHandler);
WEAK_ALIAS(UCPD1_IRQHandler);
WEAK_ALIAS(USART2_IRQHandler);
WEAK_ALIAS(ADC1_2_IRQHandler);

/* The G474 has 102 maskable interrupts.  Sizing the table to all of them
 * costs 400 bytes of flash and means adding a handler later is a one-line
 * change rather than a table resize nobody notices is needed.
 *
 * Every unused slot points at Default_Handler, not at zero.  A table padded
 * with zeroes turns a spurious interrupt into a hard fault with a null
 * stacked PC, which tells you nothing; landing in Default_Handler with the
 * IPSR still holding the exception number tells you which peripheral fired. */
#define G474_IRQ_COUNT 102

typedef void (*vector_t)(void);

#define D Default_Handler,
#define R2 D D
#define R4 R2 R2
#define R8 R4 R4
#define R16 R8 R8
#define R32 R16 R16

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + G474_IRQ_COUNT] = {
    (vector_t)(&_estack),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0,
    0,
    0,
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,

    /* IRQ 0..101.  Positional, so the runs of Default_Handler are explicit
     * and the named handlers land at the index the comment claims. */
    R16 R2                    /* IRQ  0..17 */
    ADC1_2_IRQHandler,        /* IRQ 18 */
    R2                        /* IRQ 19..20 */
    FDCAN1_IT0_IRQHandler,    /* IRQ 21 */
    FDCAN1_IT1_IRQHandler,    /* IRQ 22 */
    R8 R4 R2 D                /* IRQ 23..37 */
    USART2_IRQHandler,        /* IRQ 38 */
    R16 R8                    /* IRQ 39..62 */
    UCPD1_IRQHandler,         /* IRQ 63 */
    R32 R4 R2                 /* IRQ 64..101 */
};

#undef D
#undef R2
#undef R4
#undef R8
#undef R16
#undef R32

void Reset_Handler(void) {
    /* Copy .data out of flash, then zero .bss.  Word copies: both regions are
     * 4-byte aligned by the linker script. */
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0;
    }

    SystemInit();

    /* C++ / attribute((constructor)) initialisers, if any ever appear. */
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) {
        (*f)();
    }

    (void)main();

    for (;;) {
        __wfi();
    }
}
