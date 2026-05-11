file(GLOB MY_SOURCES "./src/*.cpp")
add_library(robot_lib ${MY_SOURCES})
target_include_directories(robot_lib PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include ${PROJECT_SOURCE_DIR}/recipe/include ${PROJECT_SOURCE_DIR}/capabilities/include)
target_link_libraries(robot_lib PUBLIC open62541 wrappers_lib recipe_lib capability_lib)