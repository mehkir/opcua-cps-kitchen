add_library(mape_interface INTERFACE)
target_include_directories(mape_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/actions/include)