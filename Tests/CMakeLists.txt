enable_testing()

# Make sure these are before you call add_subdirectory
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

# Add all test files recursively
file(GLOB_RECURSE INTERNAL_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Internal/src/*.cpp)
file(GLOB_RECURSE ROUTING_TESTS  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src/*.cpp)
file(GLOB_RECURSE UTIL_SOURCES   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src/utils/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src/utils/*.h ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src/utils/*.cpp)
file(GLOB_RECURSE GNS3_SOURCES   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src/gns3/*.hpp)
file(GLOB_RECURSE MOCK_HEADERS   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Internal/src/mocks/*.hpp)


# Your test runner (the UI you built)
set(TEST_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/TestSelection.cpp)

# All test files in one
add_executable(TestSelection
    ${TEST_MAIN}
    ${INTERNAL_TESTS}
    ${ROUTING_TESTS}
    ${GNS3_SOURCES}
    ${UTIL_SOURCES}
    ${MOCK_HEADERS}
)

# Include all test source paths
target_include_directories(TestSelection PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Internal/src
    ${CMAKE_CURRENT_SOURCE_DIR}/Internal/src/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src/gns3
    ${CMAKE_CURRENT_SOURCE_DIR}/Routing/src
    $<TARGET_PROPERTY:VirtualRouter,INTERFACE_INCLUDE_DIRECTORIES>
)

# Link everything
target_link_libraries(TestSelection PRIVATE
    VirtualRouter
    GTest::GTest
    GTest::Main
    gmock
    gmock_main
    pthread
)

# Register it for ctest
add_test(NAME TestSelection COMMAND TestSelection)
