add_library(mape_interface INTERFACE)
target_include_directories(mape_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/actions/include ${PROJECT_SOURCE_DIR}/capabilities/include)
target_link_libraries(mape_interface INTERFACE capability_lib)