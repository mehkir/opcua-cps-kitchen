file(GLOB MY_SOURCES "./src/*.cpp")
add_library(timestamp_lib ${MY_SOURCES})
target_include_directories(timestamp_lib PUBLIC ${PROJECT_SOURCE_DIR}  ${PROJECT_SOURCE_DIR}/robot/include/)