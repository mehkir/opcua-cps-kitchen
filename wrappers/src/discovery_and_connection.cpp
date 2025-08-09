#include "../include/discovery_and_connection.hpp"
#include <open62541/plugin/log_stdout.h>
#include "../include/node_browser_helper.hpp"
#include "../include/client_connection_establisher.hpp"

UA_StatusCode
discover_and_connect(UA_Client*& _client, discovery_util& _discovery_util, std::string& _endpoint, std::string _object_type_name) {
    std::vector<std::string> endpoints;
    if (_discovery_util.lookup_endpoints(endpoints) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to lookup endpoints", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    for (std::string endpoint : endpoints) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Endpoint URL: %s\n", endpoint.c_str());
        _endpoint = endpoint;
        if (node_browser_helper().has_instance(_endpoint, _object_type_name))
            break;
        _endpoint.clear();
    }
    if (_endpoint.empty()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to lookup %s endpoint.", __FUNCTION__, _object_type_name.c_str());
        return UA_STATUSCODE_BAD;
    }
    client_connection_establisher controller_client_connection_establisher;
    if (controller_client_connection_establisher.establish_connection(_client, _endpoint))
        return UA_STATUSCODE_GOOD;
    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing %s client session.", __FUNCTION__, _object_type_name.c_str());
    return UA_STATUSCODE_BAD;
}