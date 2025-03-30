# Ensure the Telnet library is found
set(TELNET_LIBRARIES /usr/lib/x86_64-linux-gnu/libtelnet.so)
set(TELNET_INCLUDE_DIR /usr/include)

# Check for Telnet library existence
if (EXISTS ${TELNET_LIBRARIES} AND EXISTS ${TELNET_INCLUDE_DIR}/libtelnet.h)
    message(STATUS "Found libtelnet: ${TELNET_LIBRARIES}")
else()
    message(FATAL_ERROR "libtelnet not found in expected paths. Please verify installation.")
endif()

# Collect all .cpp, .hpp, and .h files recursively from src/
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(FILTER SRC_FILES EXCLUDE REGEX "./main\\.cpp$")

# Create VirtualRouter library with all source files
add_library(VirtualRouter STATIC ${SRC_FILES})

# Collect all header directories recursively
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

# Collect unique directories where the headers are located
set(HEADER_DIRS "")
foreach(header ${HEADER_FILES})
    get_filename_component(HEADER_DIR ${header} DIRECTORY)
    list(APPEND HEADER_DIRS ${HEADER_DIR})
endforeach()

list(REMOVE_DUPLICATES HEADER_DIRS) # Ensure unique directories

# Add the collected directories to include path
target_include_directories(VirtualRouter PUBLIC ${HEADER_DIRS})

# Link external and common libraries
target_include_directories(VirtualRouter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# Add source files from common and external directories (if any)
file(GLOB_RECURSE COMMON_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp)
file(GLOB_RECURSE EXTERNAL_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/external/*.cpp)

# Add to the VirtualRouter library
target_sources(VirtualRouter PRIVATE ${COMMON_SRC} ${EXTERNAL_SRC})

# Link the Telnet library to the VirtualRouter
target_link_libraries(VirtualRouter PRIVATE ${TELNET_LIBRARIES})

# Link OpenSSL libraries
find_package(OpenSSL REQUIRED)
target_link_libraries(VirtualRouter PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# Link PCAP libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)
target_link_libraries(VirtualRouter PRIVATE ${PCAP_LIBRARIES})

# Now create the executable for the VirtualRouter from main.cpp
add_executable(VirtualRouterExec ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

# Link the VirtualRouter library to the executable
target_link_libraries(VirtualRouterExec PRIVATE VirtualRouter OpenSSL::SSL OpenSSL::Crypto ${PCAP_LIBRARIES} pthread)

# Add the include directories for the executable (VirtualRouter already has them)
target_include_directories(VirtualRouterExec PRIVATE ${HEADER_DIRS})
