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
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

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
    PkgConfig::HIDAPI
)
