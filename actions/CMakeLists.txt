file(GLOB MY_SOURCES "./src/*.cpp")
file(GLOB MY_HEADERS "./include/*.hpp")
add_library(actions_lib ${MY_SOURCES})
target_include_directories(actions_lib PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/robot/include)