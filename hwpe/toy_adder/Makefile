APP = toy_adder
APP_SRCS += toy_adder.c
APP_CFLAGS += -O3 -g -Iinc
APP_LDFLAGS += -Os -g

# override for leveraging the custom_hwpe added to gvsoc in this repo.
override runner_args += --target-property=chip/cluster/custom_hwpe=$(realpath model/toy_adder.py)

include $(RULES_DIR)/pmsis_rules.mk
