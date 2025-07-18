#ifndef DISCOVERY_UTIL_HPP
#define DISCOVERY_UTIL_HPP

#include <open62541/server_config_default.h>
#include <vector>
#include <string>

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
 * @param _endpoints stores the returned endpoints
 * @return UA_StatusCode the status code
 */
UA_StatusCode
lookup_endpoints(std::vector<std::string>& _endpoints);

#endif // DISCOVERY_UTIL_HPP