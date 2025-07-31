#include "../include/client_connection_establisher.hpp"
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <chrono>
#include <unistd.h>
#include "../include/filtered_logger.hpp"

#define TIMEOUT 10

client_connection_establisher::client_connection_establisher(UA_Client*& _client) : client_(_client) {
}

client_connection_establisher::~client_connection_establisher() {
}

bool
client_connection_establisher::establish_connection(std::string _server_endpoint) {
    UA_ClientConfig* client_config = UA_Client_getConfig(client_);
    UA_ClientConfig_setDefault(client_config);
    client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    client_config->timeout = 1000;
    *client_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);

    auto start = std::chrono::steady_clock::now();
    UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
    while(session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_StatusCode status = UA_Client_connect(client_, _server_endpoint.c_str());
        UA_Client_getState(client_, NULL, &session_state, NULL);
        if (session_state != UA_SESSIONSTATE_ACTIVATED) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Connection attempt failed. Retrying to connect in 1 second", __FUNCTION__);
            sleep(1);
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed >= TIMEOUT && session_state != UA_SESSIONSTATE_ACTIVATED) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Connection attempt timed out after %d seconds", __FUNCTION__, TIMEOUT);
            break;
        }
    }
    return session_state == UA_SESSIONSTATE_ACTIVATED;
}

bool
client_connection_establisher::check_and_reconnect_client() {
    bool connected = true;
    UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
    UA_Client_getState(client_, NULL, &session_state, NULL);
    if (session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_ClientConfig* client_config = UA_Client_getConfig(client_);
        std::string client_endpoint((char*) client_config->endpointUrl.data, client_config->endpointUrl.length);
        connected = establish_connection(client_endpoint);
    }
    return connected;
}