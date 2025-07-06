add_executable(information_node_tester information_node_tester.cpp)
target_link_libraries(information_node_tester PUBLIC wrappers_lib open62541)
target_include_directories(information_node_tester PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)

add_executable(recipe_testframe recipe_testframe.cpp)
target_link_libraries(recipe_testframe PUBLIC recipe_lib)
target_include_directories(recipe_testframe PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/recipe/include)

add_executable(session_id_testframe session_id_testframe.cpp)
target_include_directories(session_id_testframe PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR})

add_executable(test_now_monotonic test_now_monotonic.cpp)
target_link_libraries(test_now_monotonic PUBLIC wrappers_lib open62541)
target_include_directories(test_now_monotonic PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)