cmake_minimum_required(VERSION 3.24)
project(opcua_cps_kitchen)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(wrappers)
add_subdirectory(robot)
add_subdirectory(demos)
add_subdirectory(controller)
add_subdirectory(conveyor)

add_executable(start_robot_instance start_robot_instance.cpp)
target_link_libraries(start_robot_instance PUBLIC wrappers_lib robot_lib open62541)
target_include_directories(start_robot_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/robot/include)

add_executable(start_controller_instance start_controller_instance.cpp)
target_link_libraries(start_controller_instance PUBLIC wrappers_lib controller_lib open62541)
target_include_directories(start_controller_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/controller/include)

add_executable(start_conveyor_instance start_conveyor_instance.cpp)
target_link_libraries(start_conveyor_instance PUBLIC wrappers_lib conveyor_lib open62541)
target_include_directories(start_conveyor_instance PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/conveyor/include)