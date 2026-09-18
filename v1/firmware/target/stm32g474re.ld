/* STM32G474RET6 -- 512 KB flash, 128 KB SRAM.
 *
 * The G474's SRAM is three blocks that happen to be contiguous: SRAM1 (80 KB
 * at 0x20000000), SRAM2 (16 KB at 0x20014000) and CCM SRAM (32 KB at
 * 0x20018000).  Because they abut, one 128 KB region is correct and simpler
 * than three.  CCM is also aliased at 0x10000000 for zero-wait-state code; if
 * a later revision needs that, split this region rather than moving it.
 *
 * The heap is deliberately tiny.  Nothing in this firmware allocates -- the
 * PD policy, the CAN solver and the analog scaling are all fixed-size value
 * types -- and newlib only wants a heap symbol to link against.  If _sbrk
 * starts being called, something has quietly pulled in an allocator and that
 * is worth knowing about rather than absorbing.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_stack_size = 8K;
_heap_size  = 1K;

SECTIONS
{
    .isr_vector : ALIGN(512)
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.glue_7 .glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* Where the initialised-data image lives in flash. */
    _sidata = LOADADDR(.data);

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    /* Reserve heap and stack so the link fails loudly if they do not fit,
     * rather than at 3 a.m. when the stack grows into .bss. */
    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        _sheap = .;
        . = . + _heap_size;
        _eheap = .;
        . = ALIGN(8);
        . = . + _stack_size;
        . = ALIGN(8);
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);

    /* Note: ST's templates put libc.a/libm.a/libgcc.a in /DISCARD/ here.  Do
     * not copy that -- it throws away the C library this firmware links
     * against.  Only the non-loadable note section belongs in the bin. */
    /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
