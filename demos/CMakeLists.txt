add_executable(robot_demo robot_demo.cpp)
target_link_libraries(robot_demo PUBLIC wrappers_lib open62541)
target_include_directories(robot_demo PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(clock_demo clock_demo.cpp)
target_link_libraries(clock_demo PUBLIC wrappers_lib open62541)
target_include_directories(clock_demo PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(string_server_for_subscription string_server_for_subscription.cpp)
target_link_libraries(string_server_for_subscription PUBLIC wrappers_lib open62541)
target_include_directories(string_server_for_subscription PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(string_client_for_subscription string_client_for_subscription.cpp)
target_link_libraries(string_client_for_subscription PUBLIC wrappers_lib open62541)
target_include_directories(string_client_for_subscription PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(object_node object_node.cpp)
target_link_libraries(object_node PUBLIC wrappers_lib open62541)
target_include_directories(object_node PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)