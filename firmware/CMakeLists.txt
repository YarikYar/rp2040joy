cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(rp2040joy C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(rp2040joy
    src/main.c
    src/input.c
    src/report.c
    src/usb_hid.c
    src/usb_descriptors.c
    src/config.c
    src/flash_store.c
)

target_include_directories(rp2040joy PRIVATE
    include
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(rp2040joy
    pico_stdlib
    pico_unique_id
    hardware_adc
    hardware_gpio
    hardware_flash
    hardware_sync
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(rp2040joy)
