#ifndef DISCOVERY_UTIL_HPP
#define DISCOVERY_UTIL_HPP

#include <open62541/server_config_default.h>

/**
 * @brief Registers the server on the discovery server
 * 
 * @param _server the server
 * @return UA_StatusCode the status code
 */
UA_StatusCode
register_server(UA_Server* _server);

/**
 * @brief Deregisters the server on the discovery server
 * 
 * @param _server the server
 * @return UA_StatusCode the status code
 */
UA_StatusCode
deregister_server(UA_Server* _server);

/**
 * @brief Looks the registered server endpoints on the discovery server up
 * 
 * @param _endpoint_array_size stores the returned endpoint array size
 * @param _endpoint_array stores the returned endpoint array (you need to free manually)
 * @return UA_StatusCode the status code
 */
UA_StatusCode
lookup_endpoints(size_t* _endpoint_array_size, UA_EndpointDescription** _endpoint_array);

#endif // DISCOVERY_UTIL_HPP