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
 * @param _application_uri the application uri to filter endpoints by. If empty, all server endpoints are returned
 * @return UA_StatusCode the status code
 */
UA_StatusCode
lookup_endpoints(std::vector<std::string>& _endpoints, std::string _application_uri = "");

#endif // DISCOVERY_UTIL_HPP