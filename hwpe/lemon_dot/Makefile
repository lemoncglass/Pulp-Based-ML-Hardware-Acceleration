# Lemon Dot HWPE — Makefile
#
# Usage:
#   make clean all run          — build & run the simulation
#   make clean all run disasm   — build, run, AND dump the disassembly
#   make disasm                 — just generate the disassembly (must build first)
#   make model                  — (re)compile the GVSoC C++ model (.so)
#
# The `disasm` target generates a .dump file showing every RISC-V
# instruction the compiler generated.  This is how you can see:
#   • The `sw` instructions that program the HWPE registers (search for "10201")
#   • The `p.elw` instruction that waits for the HWPE's IRQ
#   • How the compiler lays out the matrices in memory
#   • What PULP ISA extensions are actually used

APP = lemon_dot
APP_SRCS += lemon_dot.c
APP_CFLAGS += -O2 -g -Iinc

# -O2 gives a good balance: the HWPE register writes are clear in the
# disassembly, but the compiler still does some optimisation.  Try -O0
# for the most literal translation (one C statement ≈ one block of asm)
# or -O3 to see how aggressively the compiler rearranges things.

APP_LDFLAGS += -O2 -g

# Override to load our custom HWPE model into GVSoC
override runner_args += --target-property=chip/cluster/custom_hwpe=$(realpath model/lemon_dot.py)

include $(RULES_DIR)/pmsis_rules.mk

# ---- GVSoC model compilation ----
# GVSoC loads the C++ model as a shared library (.so).  Normally cmake
# builds these during `make -C gvsoc`, but for out-of-tree HWPEs we
# compile the .so ourselves and drop it where GVSoC expects it.
#
# If you change model/lemon_dot.cpp, run `make model` (or just rebuild
# everything with `make clean model all run`).

GVSOC_ROOT   ?= $(realpath ../../gvsoc)
MODEL_SRC     = $(realpath model/lemon_dot.cpp)
MODEL_SO_NAME = $(shell python3 -c "\
	import hashlib; \
	name = '$(MODEL_SRC)'.replace('/', '_').replace('.', '_').replace('-', '_'); \
	name_hash = int(hashlib.md5(('$(MODEL_SRC)').encode()).hexdigest()[0:7], 16); \
	print(f'gen_{name}_{name_hash}.so')")
MODEL_INSTALL = $(GVSOC_ROOT)/install/models/$(MODEL_SO_NAME)

.PHONY: model
model:
	@echo "[model] Compiling $(MODEL_SRC)"
	@echo "[model]        → $(MODEL_INSTALL)"
	@g++ -shared -fPIC -O2 -std=c++17 \
		-I $(GVSOC_ROOT)/core/engine/include \
		-I $(GVSOC_ROOT)/core/include \
		-I $(GVSOC_ROOT)/install/include \
		$(MODEL_SRC) \
		-o $(MODEL_INSTALL)
	@echo "[model] Done."

# ---- Disassembly generation ----
# The ELF binary lives in BUILD/<target>/$(APP) after a successful build.
# We use riscv32-unknown-elf-objdump to disassemble it.
#
# What to look for in the .dump file:
#
#   1. Search for "10201" — every HWPE MMIO access uses this as the upper
#      20 bits of the address (loaded via `lui`).
#
#   2. The sequence around hwpe_trigger_job():
#        lui   a5, 0x10201       # base address of HWPE
#        sw    zero, 0(a5)       # write 0 to TRIGGER → starts the job
#
#   3. The p.elw instruction (PULP custom):
#        p.elw a0, 0(a5)         # event-load-word: sleep until IRQ
#      This is what eu_evt_maskWaitAndClr() compiles to.
#
#   4. The matrix data in .data section — you can see the actual int32
#      values laid out in memory.

DUMP_FILE = $(APP).dump

.PHONY: disasm
disasm:
	@ELF=$$(find BUILD -name "$(APP)" -type f 2>/dev/null | head -1); \
	if [ -z "$$ELF" ]; then \
		echo "ERROR: ELF not found. Run 'make clean all' first."; \
		exit 1; \
	fi; \
	echo "[disasm] Disassembling $$ELF → $(DUMP_FILE)"; \
	riscv32-unknown-elf-objdump -d -S -t $$ELF > $(DUMP_FILE); \
	echo "[disasm] Done. Open $(DUMP_FILE) to inspect."; \
	echo ""; \
	echo "=== Quick hints ==="; \
	echo "  grep '10201' $(DUMP_FILE)     — find all HWPE register accesses"; \
	echo "  grep 'p.elw' $(DUMP_FILE)     — find the event-wait instruction"; \
	echo "  grep 'cluster_entry' $(DUMP_FILE) — find the main HWPE driver code"
