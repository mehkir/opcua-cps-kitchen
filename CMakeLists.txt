cmake_minimum_required(VERSION 3.24)
project(opcua_cps_kitchen)

set(CMAKE_CXX_STANDARD 26)

option(USE_CUSTOM_VERSION "Use custom version" OFF)
option(USE_ASAN "Enable Address Sanitizer" OFF)
option(USE_TSAN "Enable Thread Sanitizer" OFF)

if (USE_CUSTOM_VERSION)
    # Choose your version with a variable
    set(OPEN62541_VERSION "1.4.7") # for possible versions see OPEN62541_DIR
    set(OPEN62541_DIR "${CMAKE_SOURCE_DIR}/deps/open62541-${OPEN62541_VERSION}")

    include_directories("${OPEN62541_DIR}/include")
    include_directories("${OPEN62541_DIR}/src_generated")
    include_directories("${OPEN62541_DIR}/plugins/include")
    link_directories("${OPEN62541_DIR}/lib")
endif()

add_subdirectory(statistics)
add_subdirectory(wrappers)
add_subdirectory(demos)
add_subdirectory(tests)
add_subdirectory(recipe)
add_subdirectory(actions)
add_subdirectory(capabilities)
add_subdirectory(robot)
add_subdirectory(mape_interface)
add_subdirectory(controller)
add_subdirectory(mape_implementation)
add_subdirectory(conveyor)
add_subdirectory(kitchen)

add_executable(start_robot_instance start_robot_instance.cpp)
target_link_libraries(start_robot_instance PUBLIC robot_lib)
target_include_directories(start_robot_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/robot/include)

add_executable(start_controller_instance start_controller_instance.cpp)
target_link_libraries(start_controller_instance PUBLIC controller_lib mape_lib)
target_include_directories(start_controller_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/controller/include ${PROJECT_SOURCE_DIR}/mape_implementation/include)

add_executable(start_conveyor_instance start_conveyor_instance.cpp)
target_link_libraries(start_conveyor_instance PUBLIC conveyor_lib open62541)
target_include_directories(start_conveyor_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/conveyor/include)

add_executable(start_kitchen_instance start_kitchen_instance.cpp)
target_link_libraries(start_kitchen_instance PUBLIC kitchen_lib open62541)
target_include_directories(start_kitchen_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/kitchen/include)

add_executable(statistics-writer-main statistics-writer-main.cpp)
target_link_libraries(statistics-writer-main PUBLIC statistics_lib)
target_include_directories(statistics-writer-main PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/statistics/include)

if (USE_ASAN)
    target_compile_options(start_robot_instance PRIVATE -fsanitize=address)
    target_link_options(start_robot_instance PRIVATE -fsanitize=address)
    target_compile_options(start_controller_instance PRIVATE -fsanitize=address)
    target_link_options(start_controller_instance PRIVATE -fsanitize=address)
    target_compile_options(start_conveyor_instance PRIVATE -fsanitize=address)
    target_link_options(start_conveyor_instance PRIVATE -fsanitize=address)
    target_compile_options(start_kitchen_instance PRIVATE -fsanitize=address)
    target_link_options(start_kitchen_instance PRIVATE -fsanitize=address)
endif()

if (USE_TSAN)
    target_compile_options(start_robot_instance PRIVATE -fsanitize=thread)
    target_link_options(start_robot_instance PRIVATE -fsanitize=thread)
    target_compile_options(start_controller_instance PRIVATE -fsanitize=thread)
    target_link_options(start_controller_instance PRIVATE -fsanitize=thread)
    target_compile_options(start_conveyor_instance PRIVATE -fsanitize=thread)
    target_link_options(start_conveyor_instance PRIVATE -fsanitize=thread)
    target_compile_options(start_kitchen_instance PRIVATE -fsanitize=thread)
    target_link_options(start_kitchen_instance PRIVATE -fsanitize=thread)
endif()

# Find Doxygen
find_package(Doxygen QUIET)

if (DOXYGEN_FOUND)
    # Set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # Configure the template Doxyfile
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

    # Add a custom target to build the docs
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
else ()
    message(STATUS "Doxygen not found. Documentation will not be generated.")
endif ()