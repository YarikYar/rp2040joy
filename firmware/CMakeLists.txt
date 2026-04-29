cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(rp2040joy C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Embed a build identifier into the firmware (reported via STATUS feature
# report). Override with -DRP2040JOY_BUILD_ID=... when invoking cmake;
# otherwise we ask git for a description.
if(NOT DEFINED RP2040JOY_BUILD_ID)
    execute_process(
        COMMAND git describe --always --tags --dirty --abbrev=8
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE RP2040JOY_BUILD_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT RP2040JOY_BUILD_ID)
    set(RP2040JOY_BUILD_ID "dev")
endif()
message(STATUS "rp2040joy build id: ${RP2040JOY_BUILD_ID}")

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

target_compile_definitions(rp2040joy PRIVATE
    RP2040JOY_BUILD_ID="${RP2040JOY_BUILD_ID}"
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
