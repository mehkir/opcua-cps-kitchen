file(GLOB MY_SOURCES "./src/*.cpp" "./src/*.c")
add_library(timing_config_lib ${MY_SOURCES})
target_include_directories(timing_config_lib PUBLIC ${PROJECT_SOURCE_DIR})