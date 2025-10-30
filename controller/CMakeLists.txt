file(GLOB MY_SOURCES "./src/*.cpp")
add_library(controller_lib ${MY_SOURCES})
target_include_directories(controller_lib PUBLIC ${PROJECT_SOURCE_DIR} 
                                                ${PROJECT_SOURCE_DIR}/wrappers/include
                                                ${PROJECT_SOURCE_DIR}/recipe/include
                                                ${PROJECT_SOURCE_DIR}/robot/include
                                                ${PROJECT_SOURCE_DIR}/mape_interface/include)
target_link_libraries(controller_lib PUBLIC open62541 wrappers_lib recipe_lib mape_interface)