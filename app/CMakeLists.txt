cmake_minimum_required(VERSION 3.16)

# Standalone build: cmake -B build app/  ; or via top-level -DBUILD_APP=ON.
if(NOT PROJECT_NAME)
    project(rp2040joy_app C CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Find hidapi across packagings:
#   * upstream CMake config (vcpkg / system) — target hidapi::hidapi
#   * older vcpkg ports                       — unofficial::hidapi::hidapi
#   * Linux distros split by backend          — pkg-config hidapi-hidraw / hidapi-libusb
find_package(hidapi CONFIG QUIET)
if(NOT TARGET hidapi::hidapi)
    find_package(unofficial-hidapi CONFIG QUIET)
endif()

if(TARGET hidapi::hidapi)
    set(RP2040JOY_HIDAPI_TARGET hidapi::hidapi)
elseif(TARGET unofficial::hidapi::hidapi)
    set(RP2040JOY_HIDAPI_TARGET unofficial::hidapi::hidapi)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi-hidraw)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-libusb)
    endif()
    set(RP2040JOY_HIDAPI_TARGET PkgConfig::HIDAPI)
endif()
message(STATUS "rp2040joy: hidapi target = ${RP2040JOY_HIDAPI_TARGET}")

add_executable(rp2040joy_app
    src/main.cpp
    src/MainWindow.cpp
    src/DeviceLink.cpp
    src/ConfigModel.cpp
    src/AxisCalibrator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/src/config.c
)

target_include_directories(rp2040joy_app PRIVATE
    src
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/include
)

target_link_libraries(rp2040joy_app PRIVATE
    Qt6::Widgets
    ${RP2040JOY_HIDAPI_TARGET}
)

# Hide the console window on Windows (Qt apps default to GUI subsystem).
if(WIN32)
    set_target_properties(rp2040joy_app PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
