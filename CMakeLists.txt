cmake_minimum_required(VERSION 3.24)
project(opcua_cps_kitchen)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(wrappers)
add_subdirectory(robot)

add_executable(controller controller.cpp)
target_link_libraries(controller PUBLIC wrappers_lib open62541)
target_include_directories(controller PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(robot_demo robot_demo.cpp)
target_link_libraries(robot_demo PUBLIC wrappers_lib open62541)
target_include_directories(robot_demo PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(clock clock.cpp)
target_link_libraries(clock PUBLIC wrappers_lib open62541)
target_include_directories(clock PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(start_robot_instance start_robot_instance.cpp)
target_link_libraries(start_robot_instance PUBLIC wrappers_lib robot_lib open62541)
target_include_directories(start_robot_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/robot/include)