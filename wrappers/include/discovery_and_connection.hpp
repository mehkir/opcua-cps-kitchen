#ifndef DISCOVERY_AND_CONNECTION_HPP
#define DISCOVERY_AND_CONNECTION_HPP

#include <open62541/client_highlevel.h>
#include <string>
#include <vector>
#include "../include/discovery_util.hpp"


UA_StatusCode
retry_discovery_and_connect(UA_Client*& _client, discovery_util& _discovery_util, std::string& _endpoint, std::string _object_type_name, std::atomic<bool>& _running);

#endif // DISCOVERY_AND_CONNECTION_HPP