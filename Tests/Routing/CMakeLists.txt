# Enable testing
enable_testing()

# Find all test source files
file(GLOB_RECURSE CONFIG_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Find all mock headers
file(GLOB_RECURSE UTIL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.h ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/*.cpp)

# This list will keep track of all of the test executable
set(ALL_TEST_EXECUTABLES)

foreach(test_file ${CONFIG_SOURCES})
    # Get a test name from the source file name (without extension)
    get_filename_component(test_name ${test_file} NAME_WE)

    # Create an executable for each test file
    add_executable(${test_name} ${test_file} ${UTIL_SOURCES})

    #set_target_properties(${test_name} PROPERTIES
        #RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/router"
    #)
    
    # Include necessary directories (your src and mocks directories)
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
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

