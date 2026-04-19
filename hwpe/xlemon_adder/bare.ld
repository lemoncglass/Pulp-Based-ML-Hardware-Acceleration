/* Minimal linker script for XLemon bare-metal test */
ENTRY(_start)

MEMORY
{
    L2 (rwx) : ORIGIN = 0x1C000000, LENGTH = 512K
    L1 (rw)  : ORIGIN = 0x10000000, LENGTH = 128K
}

SECTIONS
{
    .text : {
        *(.text.start)
        *(.text*)
    } > L2

    .rodata : {
        *(.rodata*)
    } > L2

    .data : {
        *(.data*)
        *(.sdata*)
    } > L2

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        __bss_end = .;
    } > L2

    /* L1 TCDM data — used by the XIF accelerator for operands/results */
    .l1_data : {
        *(.l1_data*)
    } > L1

    _stack_top = ORIGIN(L2) + LENGTH(L2);
}
