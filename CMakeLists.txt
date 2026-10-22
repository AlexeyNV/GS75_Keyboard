cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME GS75_Keyboard)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/matrix.c
    Core/Src/matrix_debounce.c
    Core/Src/anti_ghosting.c
    Core/Src/matrix_debug.c
    Core/Src/uart_rx.c
    Core/Src/keymap.c
    Core/Src/keyboard.c
    Core/Src/keyboard_runtime.c
    Core/Src/hid_keymap.c
    Core/Src/hid_fn.c
    Core/Src/hid_report.c
    Core/Src/usb_keyboard.c
    Core/Src/usb_consumer.c
    Core/Src/usb_descriptors.c
    Core/Src/usbd_keyboard.c
    Core/Src/usb_device.c
    Core/Src/runtime_timing.c
    Core/Src/usb_c_detect.c
    Core/Src/current_monitor.c
    Core/Src/power_manager.c
    Core/Src/power_debug.c
    Core/Src/power_port_stm32.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c
)

# USB is custom-owned; CubeMX .ioc and generated targets remain untouched.
target_compile_definitions(stm32cubemx INTERFACE HAL_PCD_MODULE_ENABLED)
target_include_directories(stm32cubemx INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
set(GS75_USB_VID "0x0483" CACHE STRING "USB VID: ST example default is prototype-only")
set(GS75_USB_PID "0x5710" CACHE STRING "USB PID: ST example default is prototype-only")
option(GS75_BOARD_REV2 "Enable Rev.2 GPIO/ADC diagnostics (legacy board default OFF)" OFF)
option(GS75_RGB_POWER_BENCH_TEST "Permit explicit protected dummy-load test commands" OFF)
option(GS75_POWER_HARDWARE_VERIFIED "Bench operator verified A1 BOM, wiring and dummy load" OFF)
if(GS75_RGB_POWER_BENCH_TEST AND NOT GS75_BOARD_REV2)
    message(FATAL_ERROR "Bench power testing requires GS75_BOARD_REV2=ON")
endif()
if(GS75_POWER_HARDWARE_VERIFIED AND NOT GS75_RGB_POWER_BENCH_TEST)
    message(FATAL_ERROR "Hardware acknowledgement requires GS75_RGB_POWER_BENCH_TEST=ON")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    GS75_USB_VID=${GS75_USB_VID}
    GS75_USB_PID=${GS75_USB_PID}
    GS75_BOARD_REV2=$<BOOL:${GS75_BOARD_REV2}>
    GS75_RGB_POWER_BENCH_TEST=$<BOOL:${GS75_RGB_POWER_BENCH_TEST}>
    GS75_POWER_HARDWARE_VERIFIED=$<BOOL:${GS75_POWER_HARDWARE_VERIFIED}>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
