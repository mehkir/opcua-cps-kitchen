file(GLOB MY_SOURCES "./src/*.cpp")
add_library(mape_lib ${MY_SOURCES})
target_include_directories(mape_lib PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/mape_interface/include ${PROJECT_SOURCE_DIR}/controller/include)
target_link_libraries(mape_lib PUBLIC open62541 controller_lib mape_interface)