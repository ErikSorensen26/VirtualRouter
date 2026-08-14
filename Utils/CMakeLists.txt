cmake_minimum_required(VERSION 3.16)
project(VirtualRouterUtils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(TELNET_LIBRARIES
    NAMES telnet libtelnet
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu
    REQUIRED
)

find_path(TELNET_INCLUDE_DIR
    NAMES libtelnet.h
    PATHS /usr/include
    REQUIRED
)

# Repo root and router paths
set(VR_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(VR_DIR  ${VR_ROOT}/VirtualRouter)

# ===============================
# CommandExtractor
# ===============================
add_executable(CommandExtractor
    ${CMAKE_CURRENT_LIST_DIR}/CommandExtractor/CommandExtractor.cpp
)

target_include_directories(CommandExtractor PRIVATE
    ${TELNET_INCLUDE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
    ${VR_DIR}
    ${VR_DIR}/common
    ${VR_DIR}/types
    ${VR_DIR}/external
)

target_link_libraries(CommandExtractor PRIVATE
    ${TELNET_LIBRARIES}
    #VirtualRouter
    pthread
)

set_target_properties(CommandExtractor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ===============================
# CommandParser
# ===============================
#add_executable(CommandParser
#    ${CMAKE_CURRENT_LIST_DIR}/CommandParser/CommandParser.cpp
#)
#
#target_include_directories(CommandParser PRIVATE
#    ${CMAKE_CURRENT_LIST_DIR}
#    ${VR_DIR}
#    ${VR_DIR}/common
#    ${VR_DIR}/types
#    ${VR_DIR}/external
#)
#
#target_link_libraries(CommandParser PRIVATE
#    VirtualRouter
#    pthread
#)
#
#set_target_properties(CommandParser PROPERTIES
#    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
#)

# ===============================
# LogServer (disabled intentionally)
# ===============================
# add_executable(LogServer
#     ${CMAKE_CURRENT_LIST_DIR}/LogServer/LogServer.cpp
# )
#
# target_include_directories(LogServer PRIVATE
#     ${CMAKE_CURRENT_LIST_DIR}
#     ${VR_DIR}
#     ${VR_DIR}/common
#     ${VR_DIR}/types
#     ${VR_DIR}/external
# )
#
# target_link_libraries(LogServer PRIVATE
#     VirtualRouter
#     pthread
# )
#
# set_target_properties(LogServer PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
# )

