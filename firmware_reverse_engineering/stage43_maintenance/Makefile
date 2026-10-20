SHELL      := /bin/bash
THIS_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
STAGE_NAME := 43_maintenance_commands

SHARED_ROOT  := $(THIS_DIR)/../_shared
SHARED_BSP   := $(SHARED_ROOT)/bsps/TARGET_APP_COINFT_S593
LEGACY_ROOT  := $(THIS_DIR)/../../../coinft_firmware_rebuild_mtb
PDL_ROOT     := $(LEGACY_ROOT)/libs/mtb-pdl-cat2
CORELIB_ROOT := $(LEGACY_ROOT)/libs/core-lib
CMSIS_ROOT   := $(LEGACY_ROOT)/libs/cmsis-core/CMSIS/Core/Include
CAPSENSE_ROOT := $(THIS_DIR)/../../../archives/reverse_engineering_coinft_legacy_20260903/07_analysis/infineon_reference/capsense/extracted_capsense/capsense-master
EMEEPROM_ROOT := $(THIS_DIR)/../../../archives/reverse_engineering_coinft_legacy_20260903/07_analysis/infineon_reference/emeeprom/release-v2.30.1
BLOCK_STORAGE_ROOT := $(THIS_DIR)/../../../archives/reverse_engineering_coinft_legacy_20260903/07_analysis/infineon_reference/block-storage/release-v1.0.1

TOOLCHAIN ?= /Users/550m/toolchains/arm-gnu-toolchain-13.2.Rel1-darwin-x86_64-arm-none-eabi
CC      := $(TOOLCHAIN)/bin/arm-none-eabi-gcc
OBJCOPY := $(TOOLCHAIN)/bin/arm-none-eabi-objcopy
SIZE    := $(TOOLCHAIN)/bin/arm-none-eabi-size

BUILD_DIR  := $(THIS_DIR)/build
OBJ_DIR    := $(BUILD_DIR)/obj
OUTPUT_DIR := $(BUILD_DIR)/out

DEVICE_DEFINES := \
  -DCOMPONENT_CAT2 -DCOMPONENT_CM0P -DCOMPONENT_CM0P_0 \
  -DCOMPONENT_PSOC4100SMAX -DCOMPONENT_SOFTFP -DCORE_NAME_CM0P_0=1 \
  -DCY8C4149AZI_S593 -DCY_SUPPORTS_DEVICE_VALIDATION \
  -DTARGET_APP_COINFT_S593 -DAPP_COINFT_S593

INCLUDES := \
  -I$(THIS_DIR)/include \
  -I$(CAPSENSE_ROOT) \
  -I$(EMEEPROM_ROOT) -I$(BLOCK_STORAGE_ROOT) \
  -I$(SHARED_BSP) -I$(SHARED_BSP)/config \
  -I$(PDL_ROOT)/devices/include -I$(PDL_ROOT)/devices \
  -I$(PDL_ROOT)/devices/include/ip -I$(PDL_ROOT)/drivers/include \
  -I$(PDL_ROOT)/drivers -I$(PDL_ROOT) \
  -I$(CORELIB_ROOT)/include -I$(CORELIB_ROOT) -I$(CMSIS_ROOT)

CFLAGS := -mcpu=cortex-m0plus --specs=nano.specs -mthumb -Os -g \
  -ffunction-sections -fdata-sections -ffat-lto-objects \
  -Wall -Wextra -Werror=return-type -MMD -MP \
  $(DEVICE_DEFINES) $(INCLUDES)
C_ONLY_FLAGS := -include cy_flash.h

LDFLAGS := -mcpu=cortex-m0plus --specs=nano.specs -mthumb \
  -Wl,--gc-sections -Wl,-Map,$(OUTPUT_DIR)/$(STAGE_NAME).map \
  -T$(THIS_DIR)/TOOLCHAIN_GCC_ARM/linker.ld

LOCAL_SOURCES := $(wildcard $(THIS_DIR)/src/*.c)
BSP_SOURCES := \
  $(SHARED_BSP)/startup_psoc4100smax.c \
  $(SHARED_BSP)/system_cat2.c \
  $(SHARED_BSP)/cybsp.c \
  $(SHARED_BSP)/config/cycfg.c
PDL_SOURCES := \
  $(PDL_ROOT)/drivers/source/cy_syslib.c \
  $(PDL_ROOT)/drivers/source/cy_sysclk.c \
  $(PDL_ROOT)/drivers/source/cy_scb_common.c \
  $(PDL_ROOT)/drivers/source/cy_scb_uart.c \
  $(PDL_ROOT)/drivers/source/cy_tcpwm_counter.c \
  $(PDL_ROOT)/drivers/source/cy_gpio.c \
  $(PDL_ROOT)/drivers/source/cy_flash.c \
  $(PDL_ROOT)/drivers/source/cy_msc.c
CAPSENSE_SOURCES := \
  $(CAPSENSE_ROOT)/cy_capsense_control.c \
  $(CAPSENSE_ROOT)/cy_capsense_filter.c \
  $(CAPSENSE_ROOT)/cy_capsense_generator_v3.c \
  $(CAPSENSE_ROOT)/cy_capsense_processing.c \
  $(CAPSENSE_ROOT)/cy_capsense_sensing.c \
  $(CAPSENSE_ROOT)/cy_capsense_sensing_v3.c \
  $(CAPSENSE_ROOT)/cy_capsense_structure.c \
  $(CAPSENSE_ROOT)/cy_capsense_tuner.c
STORAGE_SOURCES := \
  $(EMEEPROM_ROOT)/cy_em_eeprom.c \
  $(BLOCK_STORAGE_ROOT)/mtb_block_storage_cat2.c
C_SOURCES := $(LOCAL_SOURCES) $(BSP_SOURCES) $(PDL_SOURCES) \
  $(CAPSENSE_SOURCES) $(STORAGE_SOURCES)

ASM_SOURCE := $(PDL_ROOT)/drivers/source/COMPONENT_CM0P/TOOLCHAIN_GCC_ARM/cy_syslib_gcc.S
CAPSENSE_LIB := $(CAPSENSE_ROOT)/COMPONENT_SOFTFP/TOOLCHAIN_GCC_ARM/libcy_capsense.a

# All source basenames are unique in this image. Keeping them short avoids
# filesystem name limits when the middleware is stored in a dated archive.
source_to_object = $(OBJ_DIR)/$(notdir $(1:.c=.o))
C_OBJECTS := $(foreach src,$(C_SOURCES),$(call source_to_object,$(src)))
ASM_OBJECT := $(OBJ_DIR)/cy_syslib_gcc.o
OBJECTS := $(C_OBJECTS) $(ASM_OBJECT)
DEPS := $(C_OBJECTS:.o=.d)

ELF := $(OUTPUT_DIR)/$(STAGE_NAME).elf
HEX := $(OUTPUT_DIR)/$(STAGE_NAME).hex
BIN := $(OUTPUT_DIR)/$(STAGE_NAME).bin

.PHONY: all build clean size verify

all: build
build: $(HEX) $(BIN) size

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(ELF): $(OBJECTS) $(CAPSENSE_LIB) | $(OUTPUT_DIR)
	$(CC) $(LDFLAGS) $(OBJECTS) $(CAPSENSE_LIB) -lm -o $@

$(ASM_OBJECT): $(ASM_SOURCE) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

define COMPILE_C
$(call source_to_object,$(1)): $(1) | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(C_ONLY_FLAGS) -c $$< -o $$@
endef
$(foreach src,$(C_SOURCES),$(eval $(call COMPILE_C,$(src))))

$(OBJ_DIR) $(OUTPUT_DIR):
	mkdir -p $@

size: $(ELF)
	$(SIZE) $<

verify: build
	python3 $(THIS_DIR)/tools/verify_static.py $(TOOLCHAIN)

clean:
	rm -rf '$(BUILD_DIR)'

-include $(DEPS)
