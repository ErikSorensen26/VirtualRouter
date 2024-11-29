cmake_minimum_required(VERSION 3.10)

project(VirtualRouter)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define project root
set(VIRTUAL_ROUTER_DIR "${PROJECT_SOURCE_DIR}/VirtualRouter")
set(UTILS_DIR "${PROJECT_SOURCE_DIR}/Utils")
set(TEST_DIR "${PROJECT_SOURCE_DIR}/Tests")

# List of source files
set(VIRTUAL_ROUTER_SOURCES
    ${VIRTUAL_ROUTER_DIR}/main.cpp
    ${VIRTUAL_ROUTER_DIR}/src/Configs.cpp
    ${VIRTUAL_ROUTER_DIR}/src/Console.cpp
    ${VIRTUAL_ROUTER_DIR}/src/Interface.cpp
    ${VIRTUAL_ROUTER_DIR}/src/Terminal.cpp
    ${VIRTUAL_ROUTER_DIR}/src/CommandProcessing.cpp
    ${VIRTUAL_ROUTER_DIR}/src/Logger.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/src/Checksums.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/src/Decapsulation.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/src/Encapsulation.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/src/Ingress.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/src/Egress.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/src/Process.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Local/src/Dhcp.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Local/src/Arp.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Global/src/Eigrp.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Global/src/Rip.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Global/src/Ospf.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Global/src/Bgp.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Global/src/SaveToBinary.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Global/src/Global.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Global/src/RoutingTable.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Services/src/Time.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Services/src/TimeManager.cpp
    ${VIRTUAL_ROUTER_DIR}/Common/pugixml.cpp
    ${VIRTUAL_ROUTER_DIR}/Common/Functions.cpp
)

# Define the executable
add_executable(VirtualRouter ${VIRTUAL_ROUTER_SOURCES})

# Set include directories for the target after it's created
target_include_directories(VirtualRouter PRIVATE
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/include
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Local/include
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Global/include
    ${VIRTUAL_ROUTER_DIR}/Background/Services/include
    ${VIRTUAL_ROUTER_DIR}/Background/Global/include
    ${VIRTUAL_ROUTER_DIR}/Dir
    ${VIRTUAL_ROUTER_DIR}/Common
    ${VIRTUAL_ROUTER_DIR}/Configs
    ${VIRTUAL_ROUTER_DIR}/include
)

#------------------------------------------------------------

# Define utils include directories
set(UTILS_INCLUDE_DIRS
    ${UTILS_DIR}/
    ${UTILS_DIR}/src
    ${UTILS_DIR}/Dir
    ${UTILS_DIR}/Configs
    ${UTILS_DIR}/../VirtualRouter/Common
)

# List of source files
set(LOG_SERVER_SOURCES ${UTILS_DIR}/src/LogServer.cpp)
set(COMMAND_PARSER_SOURCES ${UTILS_DIR}/src/CommandParser.cpp)

# Define the executable
add_executable(LogServer ${LOG_SERVER_SOURCES})
add_executable(CommandParser ${COMMAND_PARSER_SOURCES})

# Apply shared utils include directories to utils
target_include_directories(LogServer PRIVATE ${UTILS_INCLUDE_DIRS})
target_include_directories(CommandParser PRIVATE ${UTILS_INCLUDE_DIRS})

#------------------------------------------------------------

# Add Tests (Google Tests)
enable_testing()
find_package(GTest REQUIRED)

# List of test files
set(TEST_SOURCES
    ${TEST_DIR}/TestEigrp.cpp
)

# Define Eigrp Test
add_executable(EigrpTest ${TEST_SOURCES})
target_link_libraries(EigrpTest PRIVATE GTest::GTest GTest::Main pthread ${PCAP_LIBRARIES} ${ZLIB_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL)
target_include_directories(EigrpTest PRIVATE $<TARGET_PROPERTY:VirtualRouter,INCLUDE_DIRECTORIES>)
add_test(NAME EigrpTest COMMAND EigrpTest)

# Python integration tests
add_custom_target(PythonTests
    COMMAND pytest ${PROJECT_SOURCE_DIR}/tests
    COMMENT "Running Pything integration tests..."
)

#------------------------------------------------------------

# Compiler flags for debugging
set(DEBUG_FlAGS "-g -O0 -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wuseless-cast -Wdouble-promotion -Wformat=2")

# Set the build type to Debug to include debugging symbols
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Use the script path in your command
execute_process(
    COMMAND git config --local alias.shrink "!bash ${PROJECT_SOURCE_DIR}/scripts/shrink_git_objects.sh"
    COMMAND git config --local alias.configinterfaces "!bash ${PROJECT_SOURCE_DIR}/scripts/add_interface.sh"
    COMMAND git shrink
)

# Find and link libpcap
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)

# Find and link ZLIB
find_package(ZLIB REQUIRED)

# Find and link OpenSSL
find_package(OpenSSL REQUIRED)

# Apply debug flags using target_compile_options
foreach(target IN LISTS VirtualRouter LogServer CommandParser EigrpTest)
    target_compile_options(${target} PRIVATE
        $<$<CONFIG:Debug>:${DEBUG_FLAGS}>
    )
endforeach()

# Link libraries to the target
target_link_libraries(VirtualRouter PRIVATE pthread ${PCAP_LIBRARIES} ${ZLIB_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL)
target_link_libraries(LogServer PRIVATE pthread ${PCAP_LIBRARIES} ${ZLIB_LIBRARIES})
target_link_libraries(CommandParser PRIVATE pthread ${PCAP_LIBRARIES} ${ZLIB_LIBRARIES})
