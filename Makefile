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

# Feature/capacity profile. Run `make clean` after changing one of these
# command-line values because make does not otherwise track CFLAGS changes.
# Examples:
#   make                                      production UI build
#   make APP_CLI_ENABLE=1                     diagnostic CLI build
#   make APP_BOOT_AUTOSTART_DEFAULTS=1        standalone demonstration build
APP_CLI_ENABLE              ?= 0
APP_BOOT_AUTOSTART_DEFAULTS ?= 0
PATTERN_GEN_MAX_SIGNALS     ?= 8
J1939_SCHED_MAX_PGNS        ?= 8

PREFIX      ?= arm-none-eabi-
CC          := $(PREFIX)gcc
OBJCOPY     := $(PREFIX)objcopy
SIZE        := $(PREFIX)size

ST_LINK_CLI ?= "C:/Program Files (x86)/STMicroelectronics/STM32 ST-LINK Utility/ST-LINK Utility/ST-LINK_CLI.exe"

LDSCRIPT    := STM32F103C8TX_FLASH.ld
ASM_SOURCES := startup_stm32f103xb.s
C_SOURCES   := $(wildcard OP/*.c) $(wildcard APP/*.c) $(wildcard MID/*.c) $(wildcard HAL/*.c)
INCLUDES    := -IOP -IAPP -IMID -IHAL

CPU         := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
CFLAGS      := $(CPU) -std=gnu11 -Os -g3 -Wall -Wextra \
               -ffunction-sections -fdata-sections -fno-common \
               $(INCLUDES) -MMD -MP \
               -DAPP_CLI_ENABLE=$(APP_CLI_ENABLE) \
               -DAPP_BOOT_AUTOSTART_DEFAULTS=$(APP_BOOT_AUTOSTART_DEFAULTS) \
               -DPATTERN_GEN_MAX_SIGNALS=$(PATTERN_GEN_MAX_SIGNALS) \
               -DJ1939_SCHED_MAX_PGNS=$(J1939_SCHED_MAX_PGNS)
ASFLAGS     := $(CPU) -x assembler-with-cpp
LDFLAGS := $(CPU) -fno-use-linker-plugin \
           -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
           -Wl,--print-memory-usage
LDLIBS      := -lm

OBJECTS     := $(addprefix $(BUILD)/,$(C_SOURCES:.c=.o)) \
               $(addprefix $(BUILD)/,$(ASM_SOURCES:.s=.o))

.PHONY: all clean flash size

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
