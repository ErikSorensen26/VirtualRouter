cmake_minimum_required(VERSION 3.16)

project(VirtualRouter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -latomic")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_BUILD_PARALLEL_LEVEL 8)

add_compile_options(-Wall -Wno-unused-parameter -fno-strict-aliasing)
add_link_options(-fuse-ld=mold -Wl,--thread-count=8)

option(BUILD_STATIC_ROUTER "Build only a full static VirtualRouterExec" OFF)
option(ENABLE_DEBUG_SYMBOLS "Include -g and -rdynamic for crash backtrace" ON)

add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/VirtualRouter/configs
            ${CMAKE_BINARY_DIR}/configs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/VirtualRouter/dir
            ${CMAKE_BINARY_DIR}/dir
    COMMENT "Copying configs/ and dir/ to build folder"
)

if (BUILD_STATIC_ROUTER)
    message(STATUS "Building in FULLY STATIC mode")

    # Force static libraries only
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(BUILD_SHARED_LIBS OFF)

    # Libpcap
    set(PCAP_ROOT /opt/static-libs/libpcap)
    find_library(PCAP_LIBRARY pcap
        HINTS ${PCAP_ROOT}/lib
        NO_DEFAULT_PATH
    )
    find_path(PCAP_INCLUDE_DIR pcap/pcap.h
        HINTS ${PCAP_ROOT}/include
        NO_DEFAULT_PATH
    )

    # Paths to static OpenSSL
    set(OPENSSL_ROOT_DIR /opt/static-libs/openssl)
    set(OPENSSL_INCLUDE_DIR /opt/static-libs/openssl/include)
    set(OPENSSL_LIBRARIES
        /opt/static-libs/openssl/lib64/libssl.a
        /opt/static-libs/openssl/lib64/libcrypto.a
    )

    # Paths to static cURL
    set(CURL_INCLUDE_DIR /opt/static-libs/curl/include)
    set(CURL_LIBRARY /opt/static-libs/curl/lib/libcurl.a)

    # Paths to static ZLIB
    set(ZLIB_LIBRARY /opt/static-libs/zlib/lib/libz.a)
    set(ZLIB_INCLUDE_DIR /opt/static-libs/zlib/include)

    # Collect all sourced directly instead of a library
    file(GLOB_RECURSE VR_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/common/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/types/*.cpp
    )

    file(GLOB_RECURSE VR_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/src/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/src/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/common/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/common/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/types/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/VirtualRouter/types/*.hpp
    )

    set(VR_INCLUDE_DIRS "")
    foreach(header ${VR_HEADERS})
        get_filename_component(HEADER_DIR ${header} DIRECTORY)
        list(APPEND VR_INCLUDE_DIRS ${HEADER_DIR})
    endforeach()
    list(REMOVE_DUPLICATES VR_INCLUDE_DIRS)

    if (ENABLE_DEBUG_SYMBOLS)
        message(STATUS "Including debug symbols (-g -gsplit-dwarf -rdynamic)")
        #add_compile_options(-g -gsplit-dwarf -rdynamic)
    else()
        message(STATUS "Debug symbols disabled (release / stripped build)")
    endif()

    add_executable(VirtualRouterExec ${VR_SOURCES})
    target_link_options(VirtualRouterExec PRIVATE -static)
    target_include_directories(VirtualRouterExec PRIVATE
        ${VR_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${CURL_INCLUDE_DIR}
        ${PCAP_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
    )
    target_link_libraries(VirtualRouterExec PRIVATE
        ${PCAP_LIBRARY}
        ${OPENSSL_LIBRARIES}
        ${CURL_LIBRARY}
        ${ZLIB_LIBRARY}
        pthread
    )

else()
    set(cmake_interprocedural_optimization false)

    # keep your debug-ish flags as-is
    add_compile_options(
        -O0
        -g2
        -ffunction-sections
        -fdata-sections
        -fno-inline-functions
        -fno-inline-small-functions
        -fsanitize=address
        #-gz
        -gdwarf-4
        #-gsplit-dwarf
        -fno-omit-frame-pointer
        -fno-ident
        -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wconversion -Wsign-conversion

    )
    add_compile_definitions(DEBUG)
    add_link_options(-Wl,--compress-debug-sections=zlib -fsanitize=address)

    # External Dependencies (visible to subdirs)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PCAP REQUIRED libpcap)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(CURL REQUIRED)

    message(STATUS "Building in NORMAL (dynamic + test + utils) mode")
    enable_testing()
    add_subdirectory(VirtualRouter)
    #add_subdirectory(Utils)
    add_subdirectory(Tests)
endif()

