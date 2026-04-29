cmake_minimum_required(VERSION 3.13)
project(rp2040joy_workspace NONE)

add_subdirectory(firmware)

option(BUILD_APP "Build Qt configuration tool" OFF)
if(BUILD_APP)
    add_subdirectory(app)
endif()
