file(GLOB MY_SOURCES "./src/*.cpp")
add_library(event_collector_lib ${MY_SOURCES})
target_include_directories(event_collector_lib PUBLIC ${PROJECT_SOURCE_DIR} 
                                                ${PROJECT_SOURCE_DIR}/wrappers/include
                                                ${PROJECT_SOURCE_DIR}/timestamp/include)
target_link_libraries(event_collector_lib PUBLIC open62541 wrappers_lib timestamp_lib)