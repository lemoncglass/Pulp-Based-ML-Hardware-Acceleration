APP = xlemon_adder_test
APP_SRCS += xlemon_adder_test.c
APP_CFLAGS += -O3 -g -Iinc
APP_LDFLAGS += -Os -g

# Use pmsis_rules.mk for cross-compilation (provides 'all' and 'clean').
# The default 'run' from pulpos targets pulp-open, which doesn't have XIF
# wiring. Use 'make sim' to run with our custom xlemon target instead.
-include $(RULES_DIR)/pmsis_rules.mk

# ---- Custom XIF target ----
XLEMON_CHIP_DIR = $(realpath chip)

# ---- Bare-metal build (no PULP SDK) ----
RISCV_CC = riscv32-unknown-elf-gcc
RISCV_OBJDUMP = riscv32-unknown-elf-objdump

BARE_SRCS = test_bare.c crt0_bare.S
BARE_ELF  = BUILD/bare/test_bare.elf
BARE_CFLAGS = -march=rv32imc -mabi=ilp32 -O2 -g -nostdlib -ffreestanding

$(BARE_ELF): $(BARE_SRCS) bare.ld
	@mkdir -p BUILD/bare
	$(RISCV_CC) $(BARE_CFLAGS) -T bare.ld -o $@ $(BARE_SRCS)
	$(RISCV_OBJDUMP) -d $@ > BUILD/bare/test_bare.dis

.PHONY: bare sim build
bare: $(BARE_ELF)

# 'make build' — compile all custom GVSoC .so files (ISS, XifDecoder, XLemonAdder, ExitModule)
build:
	./build_xlemon_target.sh

# 'make sim' — build everything, then run with our custom xlemon board
sim: build bare
	gvsoc --target-dir=$(XLEMON_CHIP_DIR) --target=xlemon_target \
		--binary=$(BARE_ELF) run
