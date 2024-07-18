cmake_minimum_required(VERSION 3.24)
project(opcua_cps_kitchen)

set(CMAKE_CXX_STANDARD 17)

add_executable(controller controller.cpp)
target_link_libraries(controller PUBLIC open62541)

add_executable(robot robot.cpp)
target_link_libraries(robot PUBLIC open62541)