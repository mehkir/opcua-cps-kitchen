#include "../include/client_connection_establisher.hpp"
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <chrono>
#include <unistd.h>
#include "../include/filtered_logger.hpp"

#define TIMEOUT 10

client_connection_establisher::client_connection_establisher() {
}

client_connection_establisher::~client_connection_establisher() {
}

bool
client_connection_establisher::establish_connection_retry(UA_Client*& _client, std::string _server_endpoint) {
    if (_client != nullptr)
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: If passed client pointer is not deleted then memory leaks will occur!");
    _client = UA_Client_new();
    UA_ClientConfig* client_config = UA_Client_getConfig(_client);
    UA_ClientConfig_setDefault(client_config);
    client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    client_config->timeout = 1000;
    // *client_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);

    auto start = std::chrono::steady_clock::now();
    UA_StatusCode status = UA_Client_connect(_client, _server_endpoint.c_str());
    while(status != UA_STATUSCODE_GOOD) {
        status = UA_Client_connect(_client, _server_endpoint.c_str());
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Connection attempt failed. Retrying to connect in 1 second", __FUNCTION__);
            sleep(1);
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed >= TIMEOUT && status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Connection attempt timed out after %d seconds", __FUNCTION__, TIMEOUT);
            break;
        }
    }
    if (status != UA_STATUSCODE_GOOD) {
        UA_Client_delete(_client);
        _client = nullptr;
    }
    return status == UA_STATUSCODE_GOOD;
}

bool
client_connection_establisher::establish_connection(UA_Client*& _client, std::string _server_endpoint) {
    if (_client != nullptr)
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: If passed client pointer is not deleted then memory leaks will occur!");
    _client = UA_Client_new();
    UA_ClientConfig* client_config = UA_Client_getConfig(_client);
    UA_ClientConfig_setDefault(client_config);
    client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    client_config->timeout = 1000;
    *client_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);

    UA_StatusCode status = UA_Client_connect(_client, _server_endpoint.c_str());
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Connection attempt failed", __FUNCTION__);
        UA_Client_delete(_client);
        _client = nullptr;
    }
    return status == UA_STATUSCODE_GOOD;
}

bool
client_connection_establisher::test_connection(std::string _server_endpoint) {
    UA_Client* test_client = UA_Client_new();
    UA_ClientConfig* client_config = UA_Client_getConfig(test_client);
    UA_ClientConfig_setDefault(client_config);
    client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    client_config->timeout = 1000;
    UA_StatusCode status = UA_Client_connect(test_client, _server_endpoint.c_str());
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Test connection status: %s", __FUNCTION__, UA_StatusCode_name(status));
    UA_Client_delete(test_client);
    return status == UA_STATUSCODE_GOOD;
}