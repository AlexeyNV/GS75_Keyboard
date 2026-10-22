cmake_minimum_required(VERSION 3.22)
project(matrix_host_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(POWER_SOURCES ../Core/Src/usb_c_detect.c ../Core/Src/current_monitor.c
    ../Core/Src/power_manager.c ../Core/Src/power_debug.c power_harness.c)
# Compile the actual firmware logic; only hardware/UART access is stubbed.
add_library(matrix_test SHARED
    ${POWER_SOURCES}
    ../Core/Src/matrix_debounce.c
    ../Core/Src/anti_ghosting.c
    ../Core/Src/matrix_debug.c
    ../Core/Src/uart_rx.c
    ../Core/Src/keymap.c
    ../Core/Src/keyboard.c
    ../Core/Src/keyboard_runtime.c
    ../Core/Src/hid_keymap.c
    ../Core/Src/hid_fn.c
    ../Core/Src/hid_report.c
    ../Core/Src/usb_keyboard.c
    ../Core/Src/usb_consumer.c
    ../Core/Src/usb_descriptors.c
    ../Core/Src/usbd_keyboard.c
    ../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    ../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    ../Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    usb_harness.c
    debug_harness.c
)
target_include_directories(matrix_test PRIVATE stubs ../Core/Inc)
target_include_directories(matrix_test PRIVATE ../Middlewares/ST/STM32_USB_Device_Library/Core/Inc)
if(MSVC)
    target_compile_options(matrix_test PRIVATE /W4 /WX)
else()
    target_compile_options(matrix_test PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME matrix_regression
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_matrix.py
            $<TARGET_FILE:matrix_test>
)
add_test(NAME keyboard_regression
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_keyboard.py
            $<TARGET_FILE:matrix_test>
)

# An old Page 07 volume compile flag must not restore the removed experiment.
add_library(hid_volume_off_test SHARED
    ../Core/Src/keymap.c ../Core/Src/keyboard.c ../Core/Src/hid_keymap.c
    ../Core/Src/hid_fn.c ../Core/Src/hid_report.c
)
target_include_directories(hid_volume_off_test PRIVATE ../Core/Inc)
target_compile_definitions(hid_volume_off_test PRIVATE HID_FN_VOLUME_ENABLED=0)
if(MSVC)
    target_compile_options(hid_volume_off_test PRIVATE /W4 /WX)
else()
    target_compile_options(hid_volume_off_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME hid_regression
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_hid.py
            $<TARGET_FILE:matrix_test> $<TARGET_FILE:hid_volume_off_test>
)
add_test(NAME consumer_regression
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_consumer.py
            $<TARGET_FILE:matrix_test> $<TARGET_FILE:hid_volume_off_test>
)

foreach(variant IN ITEMS normal bench unverified legacy)
    add_library(power_${variant} SHARED ${POWER_SOURCES})
    target_include_directories(power_${variant} PRIVATE ../Core/Inc)
    if(NOT variant STREQUAL "legacy")
        target_compile_definitions(power_${variant} PRIVATE GS75_BOARD_REV2=1)
    endif()
    if(variant STREQUAL "bench" OR variant STREQUAL "unverified")
        target_compile_definitions(power_${variant} PRIVATE GS75_RGB_POWER_BENCH_TEST=1)
    endif()
    if(variant STREQUAL "bench")
        target_compile_definitions(power_${variant} PRIVATE GS75_POWER_HARDWARE_VERIFIED=1)
    endif()
    if(MSVC)
        target_compile_options(power_${variant} PRIVATE /W4 /WX)
    else()
        target_compile_options(power_${variant} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
add_test(NAME power_regression COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_power.py $<TARGET_FILE:power_bench>
    $<TARGET_FILE:power_normal> $<TARGET_FILE:power_unverified> $<TARGET_FILE:power_legacy>)

# Full UART/HID integration with the bench power policy; same production sources.
get_target_property(MATRIX_SOURCES matrix_test SOURCES)
add_library(matrix_power_test SHARED ${MATRIX_SOURCES})
target_include_directories(matrix_power_test PRIVATE stubs ../Core/Inc
    ../Middlewares/ST/STM32_USB_Device_Library/Core/Inc)
target_compile_definitions(matrix_power_test PRIVATE GS75_BOARD_REV2=1
    GS75_RGB_POWER_BENCH_TEST=1 GS75_POWER_HARDWARE_VERIFIED=1)
if(MSVC)
    target_compile_options(matrix_power_test PRIVATE /W4 /WX)
else()
    target_compile_options(matrix_power_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME power_uart_regression COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_power_uart.py $<TARGET_FILE:matrix_power_test>)

add_test(NAME power_build_flags COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_build_flags.py ${CMAKE_COMMAND})
