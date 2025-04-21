cmake_minimum_required(VERSION 3.16)
project(VirtualRouter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Enable testing
enable_testing()

# Compiler Warnings
add_compile_options(-Wall -Wno-unused-parameter)

# External Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Add subdirectories for different parts of the project
add_subdirectory(VirtualRouter)
add_subdirectory(Utils)
add_subdirectory(Tests)
