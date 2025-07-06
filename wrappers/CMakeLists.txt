file(GLOB MY_SOURCES "./src/*.cpp" "./src/*.c")
add_library(wrappers_lib ${MY_SOURCES})
target_include_directories(wrappers_lib PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(wrappers_lib PUBLIC open62541)