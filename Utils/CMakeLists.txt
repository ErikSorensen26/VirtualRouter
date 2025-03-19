# List the specific executables and their respective source files
file(GLOB_RECURSE UTILS_EXECUTABLES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LogServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CommandParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CommandExtractor.cpp
)

# Create executable files for utils
foreach(exec_file ${UTILS_EXECUTABLES})
    get_filename_component(exec_name ${exec_file} NAME_WE)
    add_executable(${exec_name} ${exec_file})

    # Link VirtualRouter to all executables in utils
    target_link_libraries(${exec_name} PRIVATE VirtualRouter)
endforeach()

# Ensure utils can access the necessary header files
file(GLOB_RECURSE UTILS_HEADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp)

# Ensure unique header directories
set(UTILS_HEADER_DIRS "")
foreach(header ${UTILS_HEADER_FILES})
    get_filename_component(HEADER_DIR ${header} DIRECTORY)
    list(APPEND UTILS_HEADER_DIRS ${HEADER_DIR})
endforeach()

list(REMOVE_DUPLICATES UTILS_HEADER_DIRS)

# Include headers for utils
foreach(exec_file ${UTILS_EXECUTABLES})
    get_filename_component(exec_name ${exec_file} NAME_WE)
    target_include_directories(${exec_name} PRIVATE ${UTILS_HEADER_DIRS})
endforeach()
