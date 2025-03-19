# Enable testing
enable_testing()

# Find all test source files
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Find all mock headers
file(GLOB_RECURSE MOCK_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/mocks/*.hpp)

# Ensure GTest is found
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTEST REQUIRED gtest)
pkg_check_modules(GMOCK REQUIRED gmock)

# Define the tests executable
add_executable(AllTests ${TEST_SOURCES} ${MOCK_SOURCES})

# Include necessary directories
target_include_directories(AllTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Include VirtualRouter's include directory automatically
target_include_directories(AllTests PRIVATE
    $<TARGET_PROPERTY:VirtualRouter,INTERFACE_INCLUDE_DIRECTORIES>
)

# Create a test executable
target_link_libraries(AllTests PRIVATE
    VirtualRouter
    ${GTEST_LDFlAGS}
    ${GMOCK_LDFLAGS}
    pthread
)

# Register the test suit
add_test(NAME AllTests COMMAND AllTests)