# Collect all source files in external/
file(GLOB_RECURSE EXTERNAL_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Collect all header files in external/
file(GLOB_RECURSE EXTERNAL_HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# Add include directories for external headers
target_include_directories(VirtualRouter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Add external source files to the library
target_sources(VirtualRouter PRIVATE ${EXTERNAL_SRC})
