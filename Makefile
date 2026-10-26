SDK_DEMO_PATH ?= .
BL_SDK_BASE ?= ../bouffalo_sdk
TANG_BOARD ?= console60k

export BL_SDK_BASE

CHIP ?= bl616
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

cmake_definition+=-DTANG_BOARD=$(TANG_BOARD)
UI16_UART_SELFTEST ?= OFF
cmake_definition+=-DUI16_UART_SELFTEST=$(UI16_UART_SELFTEST)
UI16_SYSTEM_MENU ?= OFF
cmake_definition+=-DUI16_SYSTEM_MENU=$(UI16_SYSTEM_MENU)
UI16_SD_FONT ?= OFF
cmake_definition+=-DUI16_SD_FONT=$(UI16_SD_FONT)

include $(BL_SDK_BASE)/project.build

