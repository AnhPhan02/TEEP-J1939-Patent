# =============================================================================
# J1939 generator firmware - STM32F103C8 (Cortex-M3), bare-metal, arm-none-eabi-gcc
#
#   make            build build/j1939_generator.{elf,hex,bin}
#   make flash      program via ST-LINK_CLI (SWD) and reset
#   make size       print section sizes
#   make clean
# =============================================================================

TARGET      := j1939_generator
BUILD       := build
SCENARIO    ?= input/continuous_signals.csv
PYTHON      ?= python3
HOST_CC     ?= cc

PREFIX      ?= arm-none-eabi-
CC          := $(PREFIX)gcc
OBJCOPY     := $(PREFIX)objcopy
SIZE        := $(PREFIX)size

ST_LINK_CLI ?= "C:/Program Files (x86)/STMicroelectronics/STM32 ST-LINK Utility/ST-LINK Utility/ST-LINK_CLI.exe"

LDSCRIPT    := STM32F103C8TX_FLASH.ld
ASM_SOURCES := startup_stm32f103xb.s
C_SOURCES   := $(wildcard OP/*.c) $(wildcard APP/*.c) $(wildcard MID/*.c) $(wildcard HAL/*.c)
INCLUDES    := -IOP -IAPP -IMID -IHAL -I$(BUILD)/generated

CPU         := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
CFLAGS      := $(CPU) -std=gnu11 -Os -g3 -Wall -Wextra \
               -ffunction-sections -fdata-sections -fno-common \
               $(INCLUDES) -MMD -MP
ASFLAGS     := $(CPU) -x assembler-with-cpp
LDFLAGS := $(CPU) -fno-use-linker-plugin \
           -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
           -Wl,--print-memory-usage
LDLIBS      := -lm

OBJECTS     := $(addprefix $(BUILD)/,$(C_SOURCES:.c=.o)) \
               $(addprefix $(BUILD)/,$(ASM_SOURCES:.s=.o))

.PHONY: all clean flash size scenario FORCE

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS) $(LDSCRIPT) FORCE
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf FORCE
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf FORCE
	$(OBJCOPY) -O binary -S $< $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

flash: $(BUILD)/$(TARGET).hex
	$(ST_LINK_CLI) -c SWD UR -P $< -V -Rst

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)

# FORCE checks the selected path every invocation; unchanged output keeps its mtime.
FORCE:

$(BUILD)/export_metadata: tools/export_metadata.c MID/j1939_signal_definitions.c MID/j1939_pgn_timing.c $(wildcard MID/*.h)
	@mkdir -p $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -IMID $< MID/j1939_signal_definitions.c MID/j1939_pgn_timing.c -o $@

$(BUILD)/generated/scenario.h: FORCE $(SCENARIO) tools/scenario.py $(BUILD)/export_metadata
	$(PYTHON) tools/scenario.py --input "$(SCENARIO)" --metadata $(BUILD)/export_metadata --output $(BUILD)/generated

scenario: $(BUILD)/generated/scenario.h

HOST_MID_SOURCES := MID/j1939_pattern_generator.c MID/j1939_tx_scheduler.c MID/j1939_encode_decode.c MID/j1939_signal_definitions.c MID/j1939_pgn_timing.c MID/j1939_frame.c
.PHONY: test
test: $(BUILD)/firmware_test
	$(PYTHON) -m unittest discover -s tests -v
	$(BUILD)/firmware_test

HOST_APP_SOURCES := APP/app_generator.c APP/app_scenario.c APP/app_cli.c
$(BUILD)/firmware_test: tests/firmware_test.c $(HOST_MID_SOURCES) $(HOST_APP_SOURCES) $(wildcard MID/*.h) $(wildcard APP/*.h) $(wildcard HAL/*.h) $(BUILD)/generated/scenario.h FORCE
	@mkdir -p $(BUILD)
	$(HOST_CC) -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined $(INCLUDES) $< $(HOST_MID_SOURCES) $(HOST_APP_SOURCES) -lm -o $@

# Some supported Make versions compare only whole seconds. Force this small
# translation unit and final artifacts so rapid CSV switches cannot reuse old data.
$(BUILD)/APP/app_scenario.o: $(BUILD)/generated/scenario.h FORCE

.PHONY: expected
expected: scenario
	$(PYTHON) tools/scenario.py --input "$(SCENARIO)" --metadata $(BUILD)/export_metadata --output $(BUILD)/generated --horizon-ms "$(HORIZON_MS)"
