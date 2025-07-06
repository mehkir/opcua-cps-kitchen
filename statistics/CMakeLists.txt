file(GLOB MY_SOURCES "./src/*.cpp")
add_library(statistics_lib ${MY_SOURCES})
target_include_directories(statistics_lib PUBLIC ${PROJECT_SOURCE_DIR})