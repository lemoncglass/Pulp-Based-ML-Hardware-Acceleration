/*
 * Bare-metal test for XLemon Adder XIF on minimal GVSoC tile.
 *
 * No PULP SDK, no cluster controller — just writes to stdout and
 * uses GVSoC semihosting to exit.
 */

#include <stdint.h>

/* PULP stdout peripheral address (matches tile.py STDOUT_BASE) */
#define STDOUT_ADDR  0x1A10F000

static void putch(char c) {
    *(volatile uint32_t *)STDOUT_ADDR = (uint32_t)c;
}

static void puts_simple(const char *s) {
    while (*s) putch(*s++);
}

static void print_int(int32_t v) {
    if (v < 0) { putch('-'); v = -v; }
    char buf[12];
    int i = 0;
    if (v == 0) buf[i++] = '0';
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (--i >= 0) putch(buf[i]);
}

static void print_hex(uint32_t v) {
    puts_simple("0x");
    for (int i = 28; i >= 0; i -= 4) {
        int nibble = (v >> i) & 0xF;
        putch(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
}

/* Exit module address (matches tile.py EXIT_BASE) */
#define EXIT_ADDR  0x1A10E000

/* Write to exit module to terminate simulation. 0=success, non-zero=failure. */
static void gvsoc_exit(int status) {
    *(volatile uint32_t *)EXIT_ADDR = (uint32_t)status;
    __builtin_unreachable();
}

/*
 * xladd custom instruction — encodes as custom-1 (opcode 0b0101011).
 *
 *   31:27  26:25  24:20  19:15  14:12  11:7   6:0
 *   rs3    00     rs2    rs1    000    00000  0101011
 *
 * rs1 = pointer to operand A (in L1)
 * rs2 = pointer to operand B (in L1)
 * rs3 = pointer to result    (in L1)
 *
 * We use .insn r to emit the raw encoding with GCC inline asm.
 * The ".insn r" format: .insn r opcode, funct3, funct7, rd, rs1, rs2
 * But we need 3 source regs (R4-type). Use raw .word encoding instead.
 */
static inline void xladd(volatile int32_t *a, volatile int32_t *b, volatile int32_t *res) {
    /*
     * Build the instruction word manually:
     *   [6:0]   = 0101011  (custom-1)
     *   [11:7]  = 00000    (rd = x0, unused)
     *   [14:12] = 000      (funct3)
     *   [19:15] = rs1      (reg holding ptr to A)
     *   [24:20] = rs2      (reg holding ptr to B)
     *   [26:25] = 00
     *   [31:27] = rs3      (reg holding ptr to result)
     *
     * We put the pointers into specific registers so we can hardcode the encoding.
     * Using a10 (a0), a11 (a1), a12 (a2) as rs1, rs2, rs3.
     */
    register uint32_t r_a  __asm__("a0") = (uint32_t)a;
    register uint32_t r_b  __asm__("a1") = (uint32_t)b;
    register uint32_t r_res __asm__("a2") = (uint32_t)res;
    /* Encoding: rs3=a2(x12)=01100, 00, rs2=a1(x11)=01011, rs1=a0(x10)=01010, 000, rd=x0=00000, 0101011
     *           01100 00 01011 01010 000 00000 0101011
     *           = 0110_0001_0110_1010_0000_0000_0101_011
     * Bit by bit:
     *   31:27 = 01100 (x12)
     *   26:25 = 00
     *   24:20 = 01011 (x11)
     *   19:15 = 01010 (x10)
     *   14:12 = 000
     *   11:7  = 00000
     *    6:0  = 0101011
     * = 0x605a002b
     * Wait let me recalc:
     *   01100_00_01011_01010_000_00000_0101011
     *   = 0110 0001 0110 1010 0000 0000 0101 011(0)
     * Hmm, that's 33 bits. Let me be more careful:
     *   bit31=0, bit30=1, bit29=1, bit28=0, bit27=0 → rs3 = 01100
     *   bit26=0, bit25=0
     *   bit24=0, bit23=1, bit22=0, bit21=1, bit20=1 → rs2 = 01011
     *   bit19=0, bit18=1, bit17=0, bit16=1, bit15=0 → rs1 = 01010
     *   bit14=0, bit13=0, bit12=0 → funct3
     *   bit11=0, bit10=0, bit9=0, bit8=0, bit7=0 → rd
     *   bit6=0, bit5=1, bit4=0, bit3=1, bit2=0, bit1=1, bit0=1 → opcode
     * = 0110_0000_1011_0101_0000_0000_0010_1011
     * = 0x60B5002B
     */
    __asm__ volatile(
        ".word 0x60B5002B"  /* xladd a0, a1, a2 */
        :
        : "r"(r_a), "r"(r_b), "r"(r_res)
        : "memory"
    );
}

/* L1 TCDM data — place in a section mapped to L1 (0x10000000) */
volatile int32_t l1_operand_a  __attribute__((section(".l1_data"))) = 17;
volatile int32_t l1_operand_b  __attribute__((section(".l1_data"))) = 42;
volatile int32_t l1_result     __attribute__((section(".l1_data"))) = 0;

int main(void) {
    int pass = 1;

    puts_simple("[bare] XLemon Adder bare-metal test\n");

    /* ---- Test 1: Software addition (sanity check) ---- */
    puts_simple("[test1] Software add: ");
    int32_t sw_result = l1_operand_a + l1_operand_b;
    print_int(sw_result);
    putch('\n');
    if (sw_result != 59) {
        puts_simple("[test1] FAIL\n");
        pass = 0;
    } else {
        puts_simple("[test1] OK\n");
    }

    /* ---- Test 2: XIF xladd instruction ---- */
    puts_simple("[test2] XIF xladd: A=");
    print_int(l1_operand_a);
    puts_simple(", B=");
    print_int(l1_operand_b);
    puts_simple(" @ A=");
    print_hex((uint32_t)&l1_operand_a);
    puts_simple(", B=");
    print_hex((uint32_t)&l1_operand_b);
    puts_simple(", R=");
    print_hex((uint32_t)&l1_result);
    putch('\n');

    l1_result = 0;  /* Clear before xladd */
    xladd(&l1_operand_a, &l1_operand_b, &l1_result);

    /* After xladd, the accelerator should have written result to L1 */
    puts_simple("[test2] result = ");
    print_int(l1_result);
    putch('\n');
    if (l1_result != 59) {
        puts_simple("[test2] FAIL (expected 59)\n");
        pass = 0;
    } else {
        puts_simple("[test2] OK\n");
    }

    /* ---- Summary ---- */
    if (pass) {
        puts_simple("[bare] ALL TESTS PASSED\n");
    } else {
        puts_simple("[bare] SOME TESTS FAILED\n");
    }

    gvsoc_exit(pass ? 0 : 1);
    return 0;
}
