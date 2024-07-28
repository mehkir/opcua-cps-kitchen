add_executable(robot_demo robot_demo.cpp)
target_link_libraries(robot_demo PUBLIC wrappers_lib open62541)
target_include_directories(robot_demo PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(clock_demo clock_demo.cpp)
target_link_libraries(clock_demo PUBLIC wrappers_lib open62541)
target_include_directories(clock_demo PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)