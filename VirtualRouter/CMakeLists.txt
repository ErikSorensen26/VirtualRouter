set(TELNET_LIBRARIES /usr/lib/x86_64-linux-gnu/libtelnet.so)
set(TELNET_INCLUDE_DIR /usr/include)

if (NOT EXISTS ${TELNET_LIBRARIES} OR NOT EXISTS ${TELNET_INCLUDE_DIR}/libtelnet.h)
    message(FATAL_ERROR "libtelnet not found in expected paths. Please verify installation.")
endif()

# ----- Paths inside VirtualRouter/ -----
set(VR_ROOT     ${CMAKE_CURRENT_SOURCE_DIR})
set(VR_SRC      ${VR_ROOT}/src)
set(VR_COMMON   ${VR_ROOT}/common)
set(VR_EXTERNAL ${VR_ROOT}/external)
set(VR_TYPES    ${VR_ROOT}/types)

# Base roots (always exposed)
set(VR_PUBLIC_INCLUDE_ROOTS
    ${VR_SRC}
    ${VR_COMMON}
    ${VR_EXTERNAL}
    ${VR_TYPES}
    ${TELNET_INCLUDE_DIR}
)

# Optional additional public src paths
set(VR_PUBLIC_INCLUDE_DIRS
    global
    utils
    routing
    services
    transport
    types
)

# Expand rel paths under src/
foreach(rel ${VR_PUBLIC_INCLUDE_DIRS})
    list(APPEND VR_PUBLIC_INCLUDE_ROOTS "${VR_SRC}/${rel}")
endforeach()


# ----- Sources: dynamic, cpp-only -----
file(GLOB_RECURSE VR_SRC_CPP      CONFIGURE_DEPENDS ${VR_SRC}/*.cpp)
file(GLOB_RECURSE VR_COMMON_CPP   CONFIGURE_DEPENDS ${VR_COMMON}/*.cpp)
file(GLOB_RECURSE VR_EXTERNAL_CPP CONFIGURE_DEPENDS ${VR_EXTERNAL}/*.cpp)
file(GLOB_RECURSE VR_TYPES_CPP    CONFIGURE_DEPENDS ${VR_TYPES}/*.cpp)

# Remove main.cpp from library so it compiles alone
list(FILTER VR_SRC_CPP EXCLUDE REGEX "/main\\.cpp$")

add_library(VirtualRouter STATIC
    ${VR_SRC_CPP}
    ${VR_COMMON_CPP}
    ${VR_EXTERNAL_CPP}
    ${VR_TYPES_CPP}
)

# ----- INCLUDE POLICY (this is the entire point) -----
# 1) protocol includes: "eigrp/..." (root is src/protocol)
# 2) common/external/types stay <file> (each folder is its own root)
# 3) src is PRIVATE root (lets you include other internal headers without forcing "src/")
target_include_directories(VirtualRouter PUBLIC ${VR_PUBLIC_INCLUDE_ROOTS})

# ----- Link deps (reuse packages found at root; safe to call again if needed) -----
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)

target_include_directories(VirtualRouter PRIVATE ${PCAP_INCLUDE_DIRS})
target_link_libraries(VirtualRouter PRIVATE
    ${TELNET_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    ${PCAP_LIBRARIES}
    pthread
)

# ----- Executable: isolated compile unit -----
add_executable(VirtualRouterExec ${VR_SRC}/main.cpp)
target_link_libraries(VirtualRouterExec PRIVATE VirtualRouter)

