cmake_minimum_required(VERSION 3.22)
project(matrix_host_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Compile the actual firmware logic; only hardware/UART access is stubbed.
add_library(matrix_test SHARED
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
