
# Consider dependencies only in project.
set(CMAKE_DEPENDS_IN_PROJECT_ONLY OFF)

# The set of languages for which implicit dependencies are needed:
set(CMAKE_DEPENDS_LANGUAGES
  "ASM"
  )
# The set of files for implicit dependencies of each language:
set(CMAKE_DEPENDS_CHECK_ASM
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/portable/GCC/RISC-V/common/portASM.S" "D:/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616/build/build_out/components/os/freertos/CMakeFiles/freertos.dir/portable/GCC/RISC-V/common/portASM.S.obj"
  )
set(CMAKE_ASM_COMPILER_ID "GNU")

# Preprocessor definitions for this target.
set(CMAKE_TARGET_DEFINITIONS_ASM
  "ARCH_RISCV"
  "BFLB_TIMESTAMP_TIMEZONE=8"
  "BFLB_USE_HAL_DRIVER"
  "BFLB_USE_ROM_DRIVER"
  "BL616"
  "CONFIG_BSP_SDH_SDCARD"
  "CONFIG_CHERRYUSB"
  "CONFIG_COREDUMP"
  "CONFIG_FATFS"
  "CONFIG_FATFS_SDH_SDCARD"
  "CONFIG_FATFS_USBH"
  "CONFIG_FREERTOS"
  "CONFIG_IRQ_NUM=80"
  "CONFIG_LOG_LEVEL=3"
  "CONFIG_TLSF"
  "TANG_CONSOLE138K"
  "UI16_UART_SELFTEST"
  "configSTACK_ALLOCATION_FROM_SEPARATE_HEAP=1"
  "default_interrupt_handler=freertos_risc_v_trap_handler"
  "portasmHANDLE_INTERRUPT=interrupt_entry"
  )

# The include file search paths:
set(CMAKE_ASM_TARGET_INCLUDE_PATH
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/bsp/board/bl616dk/."
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/bsp/common/sdcard"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/fs/fatfs/."
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/fs/fatfs/port"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/libc/newlib/."
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/libc/."
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/mm/."
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/mm/tlsf"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/include"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/portable/GCC/RISC-V/common"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/portable/GCC/RISC-V/common/chip_specific_extensions/RV32I_CLINT_no_extensions"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/common"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/osal"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/core"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/cdc"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/hid"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/xbox"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/msc"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/audio"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/video"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/hub"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/usb/cherryusb/class/wireless"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/log"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/ring_buffer"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/bflb_block_pool"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/bflb_timestamp"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/getopt"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/coredump"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/cjson"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/math/include"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/utils/list"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/lhal/include"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/lhal/include/arch"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/lhal/include/arch/risc-v/t-head"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/lhal/include/arch/risc-v/t-head/Core/Include"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/lhal/include/arch/risc-v/t-head/csi_dsp/include"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/lhal/config/bl616"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/lhal/src/flash"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/soc/bl616/std/include"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/drivers/soc/bl616/std/include/hardware"
  "D:/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616/."
  "D:/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616/usb"
  "D:/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616/fpga"
  "D:/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616/ui"
  "D:/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616/core"
  "D:/Users/foobaris/Documents/Gowin/tangcore/firmware-bl616/utils"
  )

# The set of dependency files which are needed:
set(CMAKE_DEPENDS_DEPENDENCY_FILES
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/croutine.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/croutine.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/croutine.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/event_groups.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/event_groups.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/event_groups.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/freertos_port.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/freertos_port.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/freertos_port.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/list.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/list.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/list.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/portable/GCC/RISC-V/common/port.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/portable/GCC/RISC-V/common/port.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/portable/GCC/RISC-V/common/port.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/portable/MemMang/heap_3.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/portable/MemMang/heap_3.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/portable/MemMang/heap_3.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/queue.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/queue.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/queue.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/tasks.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/tasks.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/tasks.c.obj.d"
  "D:/Users/foobaris/Documents/Gowin/tangcore/bouffalo_sdk/components/os/freertos/timers.c" "build_out/components/os/freertos/CMakeFiles/freertos.dir/timers.c.obj" "gcc" "build_out/components/os/freertos/CMakeFiles/freertos.dir/timers.c.obj.d"
  )

# Targets to which this target links.
set(CMAKE_TARGET_LINKED_INFO_FILES
  )

# Fortran module output directory.
set(CMAKE_Fortran_TARGET_MODULE_DIR "")
