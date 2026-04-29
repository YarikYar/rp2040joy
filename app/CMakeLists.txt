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

# Prefer the CONFIG-mode package (vcpkg / Windows / Qt-bundled). On Linux the
# distro ships a backend-suffixed pkg-config (hidapi-hidraw on most boxes,
# hidapi-libusb on others) — fall back to that.
find_package(hidapi CONFIG QUIET)
if(hidapi_FOUND)
    set(RP2040JOY_HIDAPI_TARGET hidapi::hidapi)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi-hidraw)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-libusb)
    endif()
    set(RP2040JOY_HIDAPI_TARGET PkgConfig::HIDAPI)
endif()

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
