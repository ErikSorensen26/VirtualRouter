cmake_minimum_required(VERSION 3.16)
project(VirtualRouter)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Default to Release unless the user overrides it
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type" FORCE)
endif()

# ===== MAX PERF =====
#set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
#add_compile_options(
#    -Ofast
#    -march=native
#    -flto=auto
#    -fstrict-aliasing
#    -funroll-loops
#    -fomit-frame-pointer
#    -DNDEBUG
#    -pipe
#    # -fno-exeptions
#    # -fno-rtti
#)

# Linker optimization
add_link_options(
    -flto=auto
    -Wl,-O3
)

# (Optional) keep warnings minimal in perf builds
# add_compile_options(-Wall -Wno-unused-parameter)

# ===== DEBUG =====
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
add_compile_options(
    -O0
    -g3
    -fno-omit-frame-pointer
    -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wconversion -Wsign-conversion
)
add_compile_definitions(DEBUG)

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
