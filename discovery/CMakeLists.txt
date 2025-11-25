add_executable(discovery_server src/discovery_server.cpp)
target_link_libraries(discovery_server PRIVATE open62541 wrappers_lib)
target_include_directories(discovery_server PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/wrappers/include)
