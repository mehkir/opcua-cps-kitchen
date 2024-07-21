cmake_minimum_required(VERSION 3.24)
project(opcua_cps_kitchen)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(utilities)

add_executable(controller controller.cpp)
target_link_libraries(controller PUBLIC open62541)

add_executable(robot robot.cpp)
target_link_libraries(robot PUBLIC utilities_lib open62541)
target_include_directories(robot PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/utilities/include)