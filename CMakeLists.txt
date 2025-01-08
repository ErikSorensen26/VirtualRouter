cmake_minimum_required(VERSION 3.10)

project(VirtualRouter)

# Find and link libtelnet
set(TELNET_LIBRARIES /usr/lib/x86_64-linux-gnu/libtelnet.so)
set(TELNET_INCLUDE_DIR /usr/include)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define directories
set(VIRTUAL_ROUTER_DIR "${PROJECT_SOURCE_DIR}/VirtualRouter")
set(UTILS_DIR "${PROJECT_SOURCE_DIR}/Utils")
set(TEST_DIR "${PROJECT_SOURCE_DIR}/Tests")
set(COMMON_DIR "${VIRTUAL_ROUTER_DIR}/Common")

# Output Directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Debugging flags
add_compile_options(-Wall -Wextra -pedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wuseless-cast -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)

# External Libraries
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

set(TELNET_LIBRARIES /usr/lib/x86_64-linux-gnu/libtelnet.so)
set(TELNET_INCLUDE_DIR /usr/include)

# Check for Telnet library
if (EXISTS ${TELNET_LIBRARIES} AND EXISTS ${TELNET_INCLUDE_DIR}/libtelnet.h)
    message(STATUS "Found libtelnet: ${TELNET_LIBRARIES}")
else()
    message(FATAL_ERROR "libtelnet not found in expected paths. Please verify installation.")
endif()

# ============================================================
# VirtualRouter Library
# ============================================================

# List of source files
set(VIRTUAL_ROUTER_SOURCES
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
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Local/src/Ethernet.cpp
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Local/src/IPPacket.cpp
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

add_library(VirtualRouter STATIC ${VIRTUAL_ROUTER_SOURCES})


target_include_directories(VirtualRouter PUBLIC
    ${VIRTUAL_ROUTER_DIR}/Background/Packet/include
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Local/include
    ${VIRTUAL_ROUTER_DIR}/Background/Protocols/Global/include
    ${VIRTUAL_ROUTER_DIR}/Background/Services/include
    ${VIRTUAL_ROUTER_DIR}/Background/Global/include
    ${VIRTUAL_ROUTER_DIR}/Dir
    ${COMMON_DIR}
    ${VIRTUAL_ROUTER_DIR}/Configs
    ${VIRTUAL_ROUTER_DIR}/include
)

target_link_libraries(VirtualRouter PRIVATE pthread ${PCAP_LIBRARIES} ${ZLIB_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL)

# ============================================================
# Executables
# ============================================================

# Main VirtualRouter executable
add_executable(VirtualRouterExec ${VIRTUAL_ROUTER_DIR}/main.cpp)
target_link_libraries(VirtualRouterExec PRIVATE VirtualRouter)

# Log Server executable
add_executable(LogServer ${UTILS_DIR}/src/LogServer.cpp)
target_include_directories(LogServer PRIVATE ${UTILS_DIR})
target_link_libraries(LogServer PRIVATE VirtualRouter)

# Command Parser executable
add_executable(CommandParser ${UTILS_DIR}/src/CommandParser.cpp)
target_include_directories(CommandParser PRIVATE ${UTILS_DIR})
target_link_libraries(CommandParser PRIVATE VirtualRouter)

# ============================================================
# Tests
# ============================================================

enable_testing()

# List of test files
set(TEST_SOURCES
    ${TEST_DIR}/RouterTesting/src/RouterSetup.cpp
    ${TEST_DIR}/RouterTesting/src/TelnetManager.cpp
    # ${TEST_DIR}/ConsoleTest.cpp
    # ${TEST_DIR}/ConfigsTest.cpp
    ${TEST_DIR}/TerminalTest.cpp
)

# Test executable
add_executable(AllTests ${TEST_SOURCES})
target_link_libraries(AllTests PRIVATE VirtualRouter GTest::GTest GTest::Main gmock gmock_main pthread ${TELNET_LIBRARIES})
target_include_directories(AllTests PRIVATE
    ${TEST_DIR}
    ${TEST_DIR}/RouterTesting/include
    ${TEST_DIR}/RouterTesting/Mocks
)

add_test(NAME AllTests COMMAND AllTests)


# ============================================================
# Python Integration Tests
# ============================================================

add_custom_target(PythonTests
    COMMAND pytest ${PROJECT_SOURCE_DIR}/tests
    COMMENT "Running Python integration tests..."
)

# ============================================================
# Debugging Flags for All Targets
# ============================================================

foreach(target IN LISTS VirtualRouter VirtualRouterExec LogServer CommandParser AllTests)
    target_compile_options(${target} PRIVATE ${DEBUG_FLAGS})
endforeach()
