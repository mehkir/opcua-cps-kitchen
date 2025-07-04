#include "../include/client_connection_establisher.hpp"
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <chrono>
#include "../include/filtered_logger.hpp"

#define TIMEOUT 10

std::unordered_map<UA_Client*, client_connection_establisher*> client_connection_establisher::client_map_;

client_connection_establisher::client_connection_establisher(UA_Client* _client) : client_(_client), connected_(false) {
    client_map_[client_] = this;
}

client_connection_establisher::~client_connection_establisher() {
    client_map_.erase(client_);
}

bool
client_connection_establisher::establish_connection(std::string _server_endpoint) {
    UA_ClientConfig* client_config = UA_Client_getConfig(client_);
    UA_ClientConfig_setDefault(client_config);
    client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    client_config->stateCallback = connection_status_callback;
    // client_config->timeout = 1000;
    // *client_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    // std::string server_endpoint = "opc.tcp://localhost:" + std::to_string(_server_port);

    /* Connect to a server */
    // UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
    // int retry_counter = 0;
    // while(retry_counter < RETRY_LIMIT && session_state != UA_SESSIONSTATE_ACTIVATED) {
    //     UA_StatusCode status_code = UA_Client_connect(_client, _server_endpoint.c_str());
    //     UA_Client_getState(_client, NULL, &session_state, NULL);
    //     if(status_code != UA_STATUSCODE_GOOD || session_state != UA_SESSIONSTATE_ACTIVATED) {
    //         UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Not connected. Retrying to connect in 1 second");
    //         sleep_ms(1000);
    //         retry_counter++;
    //     }
    // }
    auto start = std::chrono::steady_clock::now();
    while(!connected_) {
        UA_Client_connectAsync(client_, _server_endpoint.c_str());
        UA_StatusCode status = UA_Client_run_iterate(client_, 100);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: %s", __FUNCTION__, UA_StatusCode_name(status));
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed >= TIMEOUT) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Connection attempt timed out after %d seconds", __FUNCTION__, TIMEOUT);
            break;
        }
    }
    return connected_;
}

void 
client_connection_establisher::connection_status_callback(UA_Client* _client, UA_SecureChannelState _channel_state, UA_SessionState _session_state, UA_StatusCode _connect_status) {
    if (client_map_.find(_client) != client_map_.end()) {
        client_connection_establisher* instance = client_map_[_client];
        instance->on_connection_status(_client, _channel_state, _session_state, _connect_status);
    }
}

void client_connection_establisher::on_connection_status(UA_Client* _client, UA_SecureChannelState _channel_state, UA_SessionState _session_state, UA_StatusCode _connect_status) {
    if(_session_state == UA_SESSIONSTATE_ACTIVATED)
        connected_ = true;
    else
        connected_ = false;
    if (connected_)
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: connected %d", __FUNCTION__, connected_);
}