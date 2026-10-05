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

$(BUILD)/$(TARGET).elf: $(OBJECTS) $(LDSCRIPT)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
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
