# Enable testing
enable_testing()

# Find all test source files
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Find all mock headers
file(GLOB_RECURSE MOCK_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/mocks/*.hpp)

# This list will keep track of all of the test executable
set(ALL_TEST_EXECUTABLES)

foreach(test_file ${TEST_SOURCES})
    # Get a test name from the source file name (without extension)
    get_filename_component(test_name ${test_file} NAME_WE)

    # Create an executable for each test file
    add_executable(${test_name} ${test_file} ${MOCK_SOURCES})

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
    )

    # Include necessary directories (your src and mocks directories)
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mocks
    )

    # Include VirtualRouter's include directory automatically
    target_include_directories(${test_name} PRIVATE
        $<TARGET_PROPERTY:VirtualRouter,INTERFACE_INCLUDE_DIRECTORIES>
    )

    # Link against main library and the GTest/GMock libraries
    target_link_libraries(${test_name} PRIVATE
        GTest::GTest
        GTest::Main
        gmock
        gmock_main
        VirtualRouter
        pthread
    )

    # Register the test with CTest (this allows running the test individually with ctest)
    add_test(NAME ${test_name} COMMAND ${test_name})

    # Add the test executable name to the list
    list(APPEND ALL_TEST_EXECUTABLES ${test_name})
endforeach()

# Build a single executable that aggregates all test source files and mocks
add_executable(AllTests ${TEST_SOURCES} ${MOCK_SOURCES})
target_include_directories(AllTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mocks
    $<TARGET_PROPERTY:VirtualRouter,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(AllTests PRIVATE
    VirtualRouter
    GTest::GTest
    GTest::Main
    gmock
    gmock_main
    pthread
)

add_test(NAME AllTests COMMAND AllTests)

