# =============================================================================
# J1939 generator firmware - STM32F103C8 (Cortex-M3), bare-metal, arm-none-eabi-gcc
#
#   make            build the 179-scalar firmware under build/profile-179/
#   make ENABLE_100_SPNS=1
#                   build experimental 100-slot capacity profile under
#                   build/profile-100/
#   make ENABLE_179_SCALAR=1
#                   build the Phase 3 179-scalar full scheduler profile under
#                   build/profile-179/
#   make flash      program via ST-LINK_CLI (SWD) and reset
#   make size       print section sizes
#   make clean
#   make catalog        regenerate generated/ from the master workbook
#   make catalog-check  fail if generated/ differs from a clean regeneration
#   make catalog-test   catalog-check + generator and generated-C host tests
#   make host-test      all host tests (legacy regression + catalog + value store)
# =============================================================================

# Feature profile. The 100-SPN selection changes capacity only; it does not
# provide the missing stable keys, PGN timing, Request service or TP support.
# Separate target/build names prevent objects from the validated 40-SPN build
# being reused by the experimental capacity build.
# Examples:
#   make                                      179-scalar production UI build
#   make ENABLE_179_SCALAR=0                  legacy 40-SPN build
#   make APP_CLI_ENABLE=1                     diagnostic CLI build
#   make ENABLE_WAVEFORM=1                    legacy SIG waveform engine
#   make APP_BOOT_AUTOSTART_DEFAULTS=1        standalone demonstration build
#   make ENABLE_100_SPNS=1                    experimental capacity build
#   make ENABLE_179_SCALAR=1                  179-scalar full scheduler build
APP_CLI_ENABLE              ?= 0
APP_BOOT_AUTOSTART_DEFAULTS ?= 0
ENABLE_100_SPNS             ?= 0
ENABLE_179_SCALAR           ?= 1
ENABLE_WAVEFORM             ?= 0

ifneq ($(filter $(ENABLE_100_SPNS),0 1),$(ENABLE_100_SPNS))
$(error ENABLE_100_SPNS must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_179_SCALAR),0 1),$(ENABLE_179_SCALAR))
$(error ENABLE_179_SCALAR must be 0 or 1)
endif
ifneq ($(filter $(ENABLE_WAVEFORM),0 1),$(ENABLE_WAVEFORM))
$(error ENABLE_WAVEFORM must be 0 or 1)
endif
ifeq ($(ENABLE_100_SPNS)$(ENABLE_179_SCALAR),11)
$(error Select at most one of ENABLE_100_SPNS and ENABLE_179_SCALAR)
endif

ifeq ($(ENABLE_179_SCALAR),1)
TARGET := j1939_generator_179scalar
BUILD  := build/profile-179
PATTERN_GEN_MAX_SIGNALS := 179
J1939_SCHED_MAX_PGNS    := 60
else ifeq ($(ENABLE_100_SPNS),1)
TARGET := j1939_generator_100spn_experimental
BUILD  := build/profile-100
PATTERN_GEN_MAX_SIGNALS := 100
J1939_SCHED_MAX_PGNS    := 60
else
TARGET := j1939_generator
BUILD  := build
PATTERN_GEN_MAX_SIGNALS := 40
J1939_SCHED_MAX_PGNS    := 11
endif

PREFIX      ?= arm-none-eabi-
CC          := $(PREFIX)gcc
OBJCOPY     := $(PREFIX)objcopy
SIZE        := $(PREFIX)size

ST_LINK_CLI ?= "C:/Program Files (x86)/STMicroelectronics/STM32 ST-LINK Utility/ST-LINK Utility/ST-LINK_CLI.exe"

LDSCRIPT    := STM32F103C8TX_FLASH.ld
ASM_SOURCES := startup_stm32f103xb.s
# MID/j1939_signal_definitions.c (generated compact catalog) is the only
# 179-record runtime database. The legacy human-readable definitions and PGN
# timing tables live in tests/support/ as host-test references, so a firmware
# link cannot silently carry both representations.
C_SOURCES   := $(wildcard OP/*.c) $(wildcard APP/*.c) $(wildcard MID/*.c) $(wildcard HAL/*.c)
INCLUDES    := -IOP -IAPP -IMID -IHAL -Igenerated

CPU         := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
CFLAGS      := $(CPU) -std=gnu11 -Os -g3 -Wall -Wextra \
               -ffunction-sections -fdata-sections -fno-common \
               $(INCLUDES) -MMD -MP \
               -DAPP_CLI_ENABLE=$(APP_CLI_ENABLE) \
               -DAPP_BOOT_AUTOSTART_DEFAULTS=$(APP_BOOT_AUTOSTART_DEFAULTS) \
               -DENABLE_100_SPNS=$(ENABLE_100_SPNS) \
               -DENABLE_179_SCALAR=$(ENABLE_179_SCALAR) \
               -DENABLE_WAVEFORM=$(ENABLE_WAVEFORM) \
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

# Single-source catalog (host tools). The generator writes the firmware
# databases MID/j1939_signal_definitions.{c,h},
# MID/j1939_customer_profiles.{c,h}, and the UI/test artifacts in generated/.
PYTHON  ?= python
HOST_CC ?= gcc
HOST_DIR := build/host
HOST_CFLAGS := -std=gnu11 -Wall -Wextra -IMID -IAPP -IHAL -IOP -Igenerated -Itests/support
CATALOG_TEST := $(HOST_DIR)/test_j1939_catalog_generated.exe
PROFILE_C := MID/j1939_customer_profiles.c
# Legacy human-readable database: host reference for compatibility tests only.
LEGACY_C := tests/support/j1939_signal_definitions_legacy.c
# Legacy tests provide their own link/HAL stubs, hence the reduced source sets.
MID_NO_STORAGE := $(filter-out MID/j1939_profile_storage.c,$(wildcard MID/*.c))
MID_NO_LINK := $(filter-out MID/j1939_link.c MID/j1939_rx_router.c MID/j1939_profile_storage.c,$(wildcard MID/*.c))
PERSIST_STUB := tests/support/app_persistence_stub.c

.PHONY: catalog catalog-check catalog-test host-test

catalog:
	$(PYTHON) tools/generate_j1939_catalog.py

catalog-check:
	$(PYTHON) tools/generate_j1939_catalog.py --check

catalog-test: catalog-check
	$(PYTHON) -m unittest tests/test_generate_j1939_catalog.py
	@mkdir -p $(dir $(CATALOG_TEST))
	$(HOST_CC) $(HOST_CFLAGS) -Werror \
		tests/test_j1939_catalog_generated.c $(PROFILE_C) $(LEGACY_C) \
		MID/j1939_signal_definitions.c MID/j1939_encode_decode.c \
		-lm -o $(CATALOG_TEST)
	$(CATALOG_TEST)

host-test: catalog-test
	$(HOST_CC) $(HOST_CFLAGS) -Werror tests/test_j1939_value_store.c \
		MID/j1939_value_store.c MID/j1939_crc32.c MID/j1939_encode_decode.c \
		MID/j1939_signal_definitions.c \
		-lm -o $(HOST_DIR)/test_j1939_value_store.exe
	$(HOST_DIR)/test_j1939_value_store.exe
	$(HOST_CC) $(HOST_CFLAGS) -Werror -DENABLE_179_SCALAR=1 \
		tests/test_j1939_scheduler_phase3.c $(MID_NO_LINK) \
		-lm -o $(HOST_DIR)/test_j1939_scheduler_phase3.exe
	$(HOST_DIR)/test_j1939_scheduler_phase3.exe
	$(HOST_CC) $(HOST_CFLAGS) -Werror -DENABLE_179_SCALAR=1 \
		tests/test_j1939_phase4a_transaction.c $(MID_NO_STORAGE) \
		APP/app_control_can.c APP/app_generator.c APP/app_boot.c $(PERSIST_STUB) \
		-lm -o $(HOST_DIR)/test_j1939_phase4a_transaction.exe
	$(HOST_DIR)/test_j1939_phase4a_transaction.exe
	$(HOST_CC) $(HOST_CFLAGS) -Werror -DENABLE_179_SCALAR=1 \
		tests/test_j1939_phase4b_protocol.c $(MID_NO_STORAGE) \
		APP/app_control_can.c APP/app_generator.c APP/app_boot.c $(PERSIST_STUB) \
		-lm -o $(HOST_DIR)/test_j1939_phase4b_protocol.exe
	$(HOST_DIR)/test_j1939_phase4b_protocol.exe
	$(HOST_CC) $(HOST_CFLAGS) -Werror -DENABLE_179_SCALAR=1 \
		tests/test_j1939_phase5_profiles.c $(MID_NO_STORAGE) \
		APP/app_control_can.c APP/app_generator.c APP/app_boot.c $(PERSIST_STUB) \
		-lm -o $(HOST_DIR)/test_j1939_phase5_profiles.exe
	$(HOST_DIR)/test_j1939_phase5_profiles.exe
	$(HOST_CC) $(HOST_CFLAGS) -Werror -DENABLE_179_SCALAR=1 \
		tests/test_j1939_phase6_persistence.c tests/support/hal_flash_fake.c \
		$(wildcard MID/*.c) APP/app_control_can.c APP/app_generator.c \
		APP/app_persistence.c APP/app_boot.c -lm \
		-o $(HOST_DIR)/test_j1939_phase6_persistence.exe
	$(HOST_DIR)/test_j1939_phase6_persistence.exe
	$(HOST_CC) $(HOST_CFLAGS) -Werror -DENABLE_179_SCALAR=1 -DOP_NO_MAIN \
		-DAPP_BOOT_AUTOSTART_DEFAULTS=0 \
		tests/test_j1939_phase7_autostart.c tests/support/hal_flash_fake.c \
		$(wildcard MID/*.c) APP/app_control_can.c APP/app_generator.c \
		APP/app_persistence.c APP/app_boot.c OP/op_main.c -lm \
		-o $(HOST_DIR)/test_j1939_phase7_autostart.exe
	$(HOST_DIR)/test_j1939_phase7_autostart.exe
	$(HOST_CC) $(HOST_CFLAGS) -DENABLE_WAVEFORM=1 \
		tests/test_j1939_r1_identity.c $(MID_NO_LINK) \
		$(LEGACY_C) -lm -o $(HOST_DIR)/test_j1939_r1_identity.exe
	$(HOST_DIR)/test_j1939_r1_identity.exe
	$(HOST_CC) $(HOST_CFLAGS) tests/test_j1939_r2_control.c MID/j1939_frame.c \
		MID/j1939_link.c MID/j1939_rx_router.c MID/j1939_signal_definitions.c \
		MID/j1939_customer_profiles.c \
		APP/app_control_can.c $(PERSIST_STUB) -lm -o $(HOST_DIR)/test_j1939_r2_control.exe
	$(HOST_DIR)/test_j1939_r2_control.exe
	$(HOST_CC) $(HOST_CFLAGS) tests/test_j1939_r3_set_const_start.c \
		$(MID_NO_STORAGE) APP/app_control_can.c APP/app_generator.c \
		APP/app_boot.c $(PERSIST_STUB) $(LEGACY_C) -lm -o $(HOST_DIR)/test_j1939_r3_set_const_start.exe
	$(HOST_DIR)/test_j1939_r3_set_const_start.exe
	$(HOST_CC) $(HOST_CFLAGS) -DENABLE_179_SCALAR=0 \
		tests/test_j1939_profile_40.c $(MID_NO_LINK) \
		APP/app_generator.c $(LEGACY_C) -lm -o $(HOST_DIR)/test_j1939_profile_40.exe
	$(HOST_DIR)/test_j1939_profile_40.exe
	$(HOST_CC) $(HOST_CFLAGS) tests/test_j1939_step1a.c $(LEGACY_C) \
		MID/j1939_encode_decode.c MID/j1939_frame.c -lm -o $(HOST_DIR)/test_j1939_step1a.exe
	$(HOST_DIR)/test_j1939_step1a.exe
	$(HOST_CC) $(HOST_CFLAGS) tests/test_j1939_build_profile.c \
		-o $(HOST_DIR)/test_j1939_build_profile.exe
	$(HOST_DIR)/test_j1939_build_profile.exe
	$(HOST_CC) $(HOST_CFLAGS) -DENABLE_179_SCALAR=1 tests/test_j1939_build_profile.c \
		-o $(HOST_DIR)/test_j1939_build_profile_179.exe
	$(HOST_DIR)/test_j1939_build_profile_179.exe

-include $(OBJECTS:.o=.d)
