cmake_minimum_required(VERSION 3.10)

project(VirtualRouter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# List of source files
set(SOURCES
    main.cpp
    src/Configs.cpp
    src/Console.cpp
    src/Interface.cpp
    src/Terminal.cpp
    src/CommandProcessing.cpp
    Background/Packet/src/Checksums.cpp
    Background/Packet/src/Decapsulation.cpp
    Background/Packet/src/Encapsulation.cpp
    Background/Packet/src/Ingress.cpp
    Background/Packet/src/Egress.cpp
    Background/Packet/src/Process.cpp
    Background/Protocols/Local/src/Dhcp.cpp
    Background/Protocols/Local/src/Arp.cpp
    Background/Protocols/Global/src/Eigrp.cpp
    Background/Protocols/Global/src/Rip.cpp
    Background/Protocols/Global/src/Ospf.cpp
    Background/Protocols/Global/src/Bgp.cpp
    Background/Global/src/SaveToBinary.cpp
    Background/Global/src/RoutingTable.cpp
    Background/Services/src/Time.cpp
    Background/Services/src/TimeManager.cpp
    Common/pugixml.cpp
    Common/Functions.cpp
)

# Define the executable
add_executable(VirtualRouter ${SOURCES})

# Set include directories for the target after it's created
target_include_directories(VirtualRouter PRIVATE
    ${PROJECT_SOURCE_DIR}/Background/Packet/include
    ${PROJECT_SOURCE_DIR}/Background/Protocols/Local/include
    ${PROJECT_SOURCE_DIR}/Background/Protocols/Global/include
    ${PROJECT_SOURCE_DIR}/Background/Services/include
    ${PROJECT_SOURCE_DIR}/Background/Global/include
    ${PROJECT_SOURCE_DIR}/Dir
    ${PROJECT_SOURCE_DIR}/Common
    ${PROJECT_SOURCE_DIR}/Configs
    ${PROJECT_SOURCE_DIR}/include
)

# Compiler flags for debugging
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wuseless-cast -Wdouble-promotion -Wformat=2")

# Set the build type to Debug to include debugging symbols
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Find and link libpcap
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)
include_directories(${PCAP_INCLUDE_DIRS})
link_directories(${PCAP_LIBRARY_DIRS})

find_package(ZLIB REQUIRED)
include_directories(${ZLIB_INCLUDE_DIRS})
link_directories(${ZLIB_INCLUDE_DIRS})

# Link libraries to the target
target_link_libraries(VirtualRouter pthread ${PCAP_LIBRARIES})
target_link_libraries(VirtualRouter ${ZLIB_LIBRARIES})