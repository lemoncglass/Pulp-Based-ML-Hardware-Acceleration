/* Minimal startup for bare-metal RISC-V — sets up stack and calls main */
.section .text.start, "ax"
.globl _start
_start:
    la sp, _stack_top
    call main
    /* GVSoC semihosting exit: a0=0x18 (SYS_EXIT), a1=0x20026 (ADP_Stopped_ApplicationExit = success) */
    li a0, 0x18
    li a1, 0x20026
    slli x0, x0, 0x1f   /* semihosting magic — tells ISS next ebreak is a syscall */
    ebreak
    j .
