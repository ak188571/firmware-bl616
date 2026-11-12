cmake_minimum_required(VERSION 3.15)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UI16 fixed-frame test option
option(UI16_UART_SELFTEST "Send fixed UI16 frame after monitor loads" OFF)
if(UI16_UART_SELFTEST)
    add_definitions(-DUI16_UART_SELFTEST)
endif()

option(UI16_SYSTEM_MENU "Display UI16 system menu with Up/Down navigation" OFF)
if(UI16_SYSTEM_MENU AND UI16_UART_SELFTEST)
    message(FATAL_ERROR "Enable only one of UI16_SYSTEM_MENU and UI16_UART_SELFTEST")
endif()
if(UI16_SYSTEM_MENU)
    add_definitions(-DUI16_SYSTEM_MENU)
endif()

option(TC_LAUNCH_TRACE "Record launch stages in RAM without UART/SD logging" OFF)
if(TC_LAUNCH_TRACE)
    if(NOT UI16_SYSTEM_MENU)
        message(FATAL_ERROR "TC_LAUNCH_TRACE requires UI16_SYSTEM_MENU")
    endif()
    add_definitions(-DTC_LAUNCH_TRACE)
endif()

# Enable only after updating and verifying the matching UI16 monitor bitstream.
option(UI16_SD_FONT "Use the microSD Unicode font pack (requires updated monitor)" OFF)
if(UI16_SD_FONT AND NOT UI16_SYSTEM_MENU)
    message(FATAL_ERROR "UI16_SD_FONT requires UI16_SYSTEM_MENU")
endif()
if(UI16_SD_FONT)
    add_definitions(-DUI16_SD_FONT)
endif()
option(UI16_RELIABLE_FONT "CRC32 and ACK transport for the microSD font" ON)
if(UI16_RELIABLE_FONT AND UI16_SD_FONT)
    add_definitions(-DUI16_RELIABLE_FONT)
endif()

option(TC_ROM_UART "Enable negotiated ROMXFR1 UART DMA transfers on Console138K" OFF)
option(TC_ROM_SPI "Use SPI DATA on a SPIDAT1 capable SNES core" OFF)
if(TC_ROM_SPI)
    if(NOT TC_ROM_UART)
        message(FATAL_ERROR "TC_ROM_SPI uses the negotiated ROMXFR1 UART control path")
    endif()
    add_definitions(-DTC_ROM_SPI)
endif()
option(TC_ROM_CPU_PIPELINE "Run common ROM preparation and ACK work during send" ON)
if(TC_ROM_UART)
    if(NOT UI16_SYSTEM_MENU OR NOT TANG_BOARD STREQUAL "console138k")
        message(FATAL_ERROR "TC_ROM_UART requires UI16_SYSTEM_MENU and console138k")
    endif()
    add_definitions(-DTC_ROM_UART)
    if(TC_ROM_CPU_PIPELINE)
        add_definitions(-DTC_ROM_CPU_PIPELINE)
    endif()
endif()

set(CONFIG_FREERTOS 1)
option(TC_ROM_PROFILE "Partition ROM CPU/CRC/SD/TX/ACK time in RAM" OFF)
if(TC_ROM_PROFILE)
    if(NOT TC_LAUNCH_TRACE OR NOT TC_ROM_UART)
        message(FATAL_ERROR "TC_ROM_PROFILE requires TC_LAUNCH_TRACE and TC_ROM_UART")
    endif()
    add_definitions(-DTC_ROM_PROFILE)
endif()
set(CONFIG_CHERRYUSB 1)
set(CONFIG_CHERRYUSB_HOST 1)
set(CONFIG_BSP_SDH_SDCARD 1)
set(CONFIG_FATFS 1)
set(CONFIG_FATFS_USBH 1)
set(CONFIG_FATFS_SDH_SDCARD 1)

if(TANG_BOARD STREQUAL "mega60k")
    add_definitions(-DTANG_MEGA60K)
elseif(TANG_BOARD STREQUAL "mega138k")
    add_definitions(-DTANG_MEGA138K)
elseif(TANG_BOARD STREQUAL "console60k")
    add_definitions(-DTANG_CONSOLE60K)
elseif(TANG_BOARD STREQUAL "console138k")
    add_definitions(-DTANG_CONSOLE138K)
elseif(TANG_BOARD STREQUAL "primer25k")
    add_definitions(-DTANG_PRIMER25K)
elseif(TANG_BOARD STREQUAL "nano20k")
    add_definitions(-DTANG_NANO20K)
endif()

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})
if(TC_ROM_UART)
    sdk_add_link_options("-Wl,--wrap=bflb_uart_put" "-Wl,--wrap=bflb_uart_putchar")
endif()
if(UI16_SYSTEM_MENU AND CHIP STREQUAL "bl616")
    # Keep the SDK immutable; wrap the two FatFs SDH entry points in this app.
    sdk_add_link_options("-Wl,--wrap=SDH_ReadMultiBlocks" "-Wl,--wrap=SDH_WriteMultiBlocks")
endif()

# Add include directories
sdk_add_include_directories(
    . usb fpga ui core utils
)

# Add source files
file(GLOB_RECURSE MY_SOURCES CONFIGURE_DEPENDS
    "usb/*.cpp"
    "fpga/*.cpp"
    "ui/*.cpp"
    "core/*.cpp"
    "utils/*.cpp"
)

target_sources(app PRIVATE ${MY_SOURCES})
sdk_set_main_file(main.cpp)

project(tangcore CXX)
