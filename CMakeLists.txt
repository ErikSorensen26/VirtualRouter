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
            ${CMAKE_SOURCE_DIR}/VirtualRouter/commands
            ${CMAKE_BINARY_DIR}/commands
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/VirtualRouter/dir
            ${CMAKE_BINARY_DIR}/dir
    COMMENT "Copying configs/, commands/ and dir/ to build folder"
)

if (BUILD_STATIC_ROUTER)
    message(STATUS "Building in FULLY STATIC mode")

    # Force static libraries only
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(BUILD_SHARED_LIBS OFF)

    # Paths to static OpenSSL
    set(OPENSSL_ROOT_DIR /opt/static-libs/openssl)
    set(OPENSSL_INCLUDE_DIR /opt/static-libs/openssl/include)
    set(OPENSSL_LIBRARIES
        /opt/static-libs/openssl/lib64/libssl.a
        /opt/static-libs/openssl/lib64/libcrypto.a
    )

    if (ENABLE_DEBUG_SYMBOLS)
        message(STATUS "Including debug symbols (-g -gsplit-dwarf -rdynamic)")
        add_compile_options(-g -gsplit-dwarf)
    else()
        message(STATUS "Debug symbols disabled (release / stripped build)")
        add_compile_options(-O2 -ffunction-sections -fdata-sections -ffat-lto-objects)
        add_link_options(-Wl,--gc-sections -s)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    add_subdirectory(VirtualRouter)

else()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

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
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(CURL REQUIRED)

    message(STATUS "Building in NORMAL (dynamic + test + utils) mode")
    enable_testing()
    add_subdirectory(VirtualRouter)
    #add_subdirectory(Utils)
    add_subdirectory(Tests)
endif()

