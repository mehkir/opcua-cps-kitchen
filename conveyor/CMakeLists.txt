file(GLOB MY_SOURCES "./src/*.cpp")
add_library(conveyor_lib ${MY_SOURCES})
target_include_directories(conveyor_lib PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include ${PROJECT_SOURCE_DIR}/robot/include)
target_link_libraries(conveyor_lib PUBLIC open62541 wrappers_lib)