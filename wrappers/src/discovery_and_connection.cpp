#include "../include/discovery_and_connection.hpp"
#include <open62541/plugin/log_stdout.h>
#include "../include/node_browser_helper.hpp"
#include "../include/client_connection_establisher.hpp"

UA_StatusCode
retry_discovery_and_connect(UA_Client* _client, discovery_util& _discovery_util, std::string& _endpoint, std::string _object_type_name, std::atomic<bool>& _running) {
    std::vector<std::string> endpoints;
    while (_running) {
        if (_discovery_util.lookup_endpoints_repeatedly(endpoints) != UA_STATUSCODE_GOOD) {
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
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to lookup %s endpoint. Retrying in %d seconds", __FUNCTION__, _object_type_name.c_str(), LOOKUP_INTERVAL);
            std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
            continue;
        }
        client_connection_establisher controller_client_connection_establisher;
        if (controller_client_connection_establisher.establish_connection(_client, _endpoint))
            break;
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing %s client session. Retrying to lookup again in %d seconds", __FUNCTION__, _object_type_name.c_str(), LOOKUP_INTERVAL);
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
    }
    return _running ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BAD;
}