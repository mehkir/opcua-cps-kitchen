file(GLOB MY_SOURCES "./src/*.cpp")
add_library(kitchen_lib ${MY_SOURCES})
target_include_directories(kitchen_lib PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include ${PROJECT_SOURCE_DIR}/recipe/include)
target_link_libraries(kitchen_lib PUBLIC open62541 wrappers_lib recipe_lib)