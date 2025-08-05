# Debugger/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(edb)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

# Gather all sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

# Get unique include dirs
set(INCLUDE_DIRS "")
foreach(hdr ${HEADERS})
    get_filename_component(dir ${hdr} DIRECTORY)
    list(APPEND INCLUDE_DIRS ${dir})
endforeach()
list(REMOVE_DUPLICATES INCLUDE_DIRS)

add_executable(edb ${SOURCES})
target_include_directories(edb PRIVATE ${INCLUDE_DIRS})

find_package(Threads REQUIRED)
target_link_libraries(edb PRIVATE Threads::Threads)