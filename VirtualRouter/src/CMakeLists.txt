# Collect all .cpp files from src/ and subdirectories
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Add the collected .cpp files to VirtualRouter
target_sources(VirtualRouter PRIVATE ${SRC_FILES})

# Collect all header files (.hpp, .h) from src/ and subdirectories
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# Collect unique directories containing headers
set(HEADER_DIRS "")
foreach(header ${HEADER_FILES})
    get_filename_component(HEADER_DIR ${header} DIRECTORY)
    list(APPEND HEADER_DIRS ${HEADER_DIR})
endforeach()

# Remove duplicates from directories
list(REMOVE_DUPLICATES HEADER_DIRS)

# Add the header directories to the include path
target_include_directories(VirtualRouter PUBLIC ${HEADER_DIRS})

# Debugging: Print the include directories to verify
message(STATUS "Include directories: ${HEADER_DIRS}")
