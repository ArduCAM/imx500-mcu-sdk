SDK_ROOT := $(abspath ../../../../..)
MICROPYTHON_DIR := $(SDK_ROOT)/third_party/micropython
RP2_DIR := $(MICROPYTHON_DIR)/ports/rp2
USER_C_MODULES := $(abspath usermod/imx500_mcu_sdk/micropython.cmake)

BOARD ?= RPI_PICO
MODULE_MODE ?= full
BUILD_DIR ?= build-$(BOARD)-imx500-$(MODULE_MODE)

DETECTED_TOOLCHAIN_PATH := $(shell if command -v arm-none-eabi-gcc >/dev/null 2>&1; then dirname "$$(command -v arm-none-eabi-gcc)"; elif [ -d "$(HOME)/.pico-sdk/toolchain" ]; then find "$(HOME)/.pico-sdk/toolchain" -path "*/bin/arm-none-eabi-gcc" -type f 2>/dev/null | sort | tail -n 1 | xargs dirname; fi)

DETECTED_PICOTOOL_PATH := $(shell if [ -d "$(HOME)/.pico-sdk/picotool" ]; then find "$(HOME)/.pico-sdk/picotool" -path "*/picotool/picotoolConfig.cmake" -type f 2>/dev/null | sort | tail -n 1 | xargs dirname | xargs dirname; fi)

ifneq ($(DETECTED_TOOLCHAIN_PATH),)
PICO_TOOLCHAIN_PATH ?= $(DETECTED_TOOLCHAIN_PATH)
endif

ifneq ($(DETECTED_PICOTOOL_PATH),)
PICOTOOL_FETCH_FROM_GIT_PATH ?= $(DETECTED_PICOTOOL_PATH)
endif

.PHONY: all mpy-cross firmware clean deploy submodules print-config

all: firmware

mpy-cross:
	$(MAKE) -C $(MICROPYTHON_DIR)/mpy-cross

firmware: mpy-cross
	IMX500_MICROPY_BUILD_MODE=$(MODULE_MODE) $(MAKE) -C $(RP2_DIR) \
		BOARD=$(BOARD) \
		BUILD=$(BUILD_DIR) \
		USER_C_MODULES=$(USER_C_MODULES) \
		PICO_TOOLCHAIN_PATH="$(PICO_TOOLCHAIN_PATH)" \
		PICOTOOL_FETCH_FROM_GIT_PATH="$(PICOTOOL_FETCH_FROM_GIT_PATH)"

clean:
	$(MAKE) -C $(RP2_DIR) BOARD=$(BOARD) BUILD=$(BUILD_DIR) clean

deploy:
	IMX500_MICROPY_BUILD_MODE=$(MODULE_MODE) $(MAKE) -C $(RP2_DIR) \
		BOARD=$(BOARD) \
		BUILD=$(BUILD_DIR) \
		USER_C_MODULES=$(USER_C_MODULES) \
		PICO_TOOLCHAIN_PATH="$(PICO_TOOLCHAIN_PATH)" \
		PICOTOOL_FETCH_FROM_GIT_PATH="$(PICOTOOL_FETCH_FROM_GIT_PATH)" \
		deploy

submodules:
	git submodule update --init --recursive third_party/micropython

print-config:
	@echo "SDK_ROOT=$(SDK_ROOT)"
	@echo "MICROPYTHON_DIR=$(MICROPYTHON_DIR)"
	@echo "BOARD=$(BOARD)"
	@echo "MODULE_MODE=$(MODULE_MODE)"
	@echo "BUILD_DIR=$(BUILD_DIR)"
	@echo "USER_C_MODULES=$(USER_C_MODULES)"
	@echo "PICO_TOOLCHAIN_PATH=$(PICO_TOOLCHAIN_PATH)"
	@echo "PICOTOOL_FETCH_FROM_GIT_PATH=$(PICOTOOL_FETCH_FROM_GIT_PATH)"
