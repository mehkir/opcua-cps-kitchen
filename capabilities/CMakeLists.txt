file(GLOB MY_SOURCES "./src/*.cpp")
add_library(capability_lib ${MY_SOURCES})
target_include_directories(capability_lib PUBLIC ${PROJECT_SOURCE_DIR}/actions/include)
target_link_libraries(capability_lib PUBLIC jsoncpp actions_lib)