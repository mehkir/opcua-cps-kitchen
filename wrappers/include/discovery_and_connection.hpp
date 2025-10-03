/**
 * @file discovery_and_connection.hpp
 * @brief Composite operation to discover an endpoint hosting a specific object type and connect.
 *
 * Uses a discovery_util instance to enumerate endpoints and attempts to connect
 * to the first one that contains an instance of the specified object type.
 */
#ifndef DISCOVERY_AND_CONNECTION_HPP
#define DISCOVERY_AND_CONNECTION_HPP

#include <open62541/client_highlevel.h>
#include <string>
#include <vector>
#include "../include/discovery_util.hpp"

/**
 * @brief Discovers and connects to the first endpoint it finds with an instance of the given object type.
 * 
 * @param _client the client.
 * @param _discovery_util the discovery util.
 * @param _endpoint the discovered endpoint.
 * @param _object_type_name the object type name.
 * @return UA_StatusCode the status code.
 */
UA_StatusCode
discover_and_connect(UA_Client*& _client, discovery_util& _discovery_util, std::string& _endpoint, std::string _object_type_name);

#endif // DISCOVERY_AND_CONNECTION_HPP