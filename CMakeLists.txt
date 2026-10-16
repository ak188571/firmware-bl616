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

set(CONFIG_FREERTOS 1)
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

# Add include directories
sdk_add_include_directories(
    . usb fpga ui core utils
)

# Add source files
file(GLOB_RECURSE MY_SOURCES
    "usb/*.cpp"
    "fpga/*.cpp"
    "ui/*.cpp"
    "core/*.cpp"
    "utils/*.cpp"
)

target_sources(app PRIVATE ${MY_SOURCES})
sdk_set_main_file(main.cpp)

project(tangcore CXX)

