# Collect all source files in common/
file(GLOB_RECURSE COMMON_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Collect all header files in common/
file(GLOB_RECURSE COMMON_HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# Add include directories for common headers
target_include_directories(VirtualRouter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Add common source files to the library
target_sources(VirtualRouter PRIVATE ${COMMON_SRC})
