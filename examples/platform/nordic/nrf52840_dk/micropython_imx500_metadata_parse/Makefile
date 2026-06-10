SDK_ROOT := $(abspath ../../../../..)
MICROPYTHON_DIR := $(SDK_ROOT)/third_party/micropython
NRF_DIR := $(MICROPYTHON_DIR)/ports/nrf
USER_C_MODULES := $(SDK_ROOT)/python_bindings/micropython/usermod

BOARD ?= PCA10056
MODULE_MODE ?= full
BUILD_DIR ?= build-$(BOARD)-imx500-nrf52840-$(MODULE_MODE)
FLASHER ?= jlink
IMX500_MICROPY_PLATFORM ?= nrf52840_dk
LTO ?= 0

DETECTED_TOOLCHAIN_PATH := $(shell if command -v arm-none-eabi-gcc >/dev/null 2>&1; then dirname "$$(command -v arm-none-eabi-gcc)"; elif [ -d "$(HOME)/.pico-sdk/toolchain" ]; then find "$(HOME)/.pico-sdk/toolchain" -path "*/bin/arm-none-eabi-gcc" -type f 2>/dev/null | sort | tail -n 1 | xargs dirname; fi)

ifneq ($(DETECTED_TOOLCHAIN_PATH),)
CROSS_COMPILE ?= $(DETECTED_TOOLCHAIN_PATH)/arm-none-eabi-
endif

.PHONY: all mpy-cross firmware clean deploy submodules print-config

all: firmware

mpy-cross:
	$(MAKE) -C $(MICROPYTHON_DIR)/mpy-cross

firmware: mpy-cross
	IMX500_MICROPY_BUILD_MODE=$(MODULE_MODE) \
	IMX500_MICROPY_PLATFORM=$(IMX500_MICROPY_PLATFORM) \
	$(MAKE) -C $(NRF_DIR) \
		BOARD=$(BOARD) \
		BUILD=$(BUILD_DIR) \
		USER_C_MODULES=$(USER_C_MODULES) \
		CROSS_COMPILE="$(CROSS_COMPILE)" \
		LTO=$(LTO) \
		FLASHER=$(FLASHER)

clean:
	$(MAKE) -C $(NRF_DIR) BOARD=$(BOARD) BUILD=$(BUILD_DIR) clean

deploy:
	IMX500_MICROPY_BUILD_MODE=$(MODULE_MODE) \
	IMX500_MICROPY_PLATFORM=$(IMX500_MICROPY_PLATFORM) \
	$(MAKE) -C $(NRF_DIR) \
		BOARD=$(BOARD) \
		BUILD=$(BUILD_DIR) \
		USER_C_MODULES=$(USER_C_MODULES) \
		CROSS_COMPILE="$(CROSS_COMPILE)" \
		LTO=$(LTO) \
		FLASHER=$(FLASHER) \
		deploy

submodules:
	git -C $(SDK_ROOT) submodule update --init --recursive third_party/micropython
	$(MAKE) -C $(NRF_DIR) submodules

print-config:
	@echo "SDK_ROOT=$(SDK_ROOT)"
	@echo "MICROPYTHON_DIR=$(MICROPYTHON_DIR)"
	@echo "BOARD=$(BOARD)"
	@echo "MODULE_MODE=$(MODULE_MODE)"
	@echo "BUILD_DIR=$(BUILD_DIR)"
	@echo "USER_C_MODULES=$(USER_C_MODULES)"
	@echo "CROSS_COMPILE=$(CROSS_COMPILE)"
	@echo "FLASHER=$(FLASHER)"
	@echo "IMX500_MICROPY_PLATFORM=$(IMX500_MICROPY_PLATFORM)"
	@echo "LTO=$(LTO)"
