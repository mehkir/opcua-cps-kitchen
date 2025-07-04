#include "../include/client_connection_establisher.hpp"
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <string>
#include <unistd.h>
#include "../include/filtered_logger.hpp"

#define RETRY_LIMIT 10
#define sleep_ms(ms) usleep(ms * 1000)

client_connection_establisher::client_connection_establisher() {
}

client_connection_establisher::~client_connection_establisher() {
}

UA_SessionState client_connection_establisher::establish_connection(UA_Client* _client, UA_UInt16 _server_port) {
    UA_ClientConfig* client_config = UA_Client_getConfig(_client);
    UA_ClientConfig_setDefault(client_config);
    client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    client_config->timeout = 1000;
    // *client_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    std::string server_endpoint = "opc.tcp://localhost:" + std::to_string(_server_port);

    /* Connect to a server */
    UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
    int retry_counter = 0;
    while(retry_counter < RETRY_LIMIT && session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_StatusCode status_code = UA_Client_connect(_client, server_endpoint.c_str());
        UA_Client_getState(_client, NULL, &session_state, NULL);
        if(status_code != UA_STATUSCODE_GOOD || session_state != UA_SESSIONSTATE_ACTIVATED) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Not connected. Retrying to connect in 1 second");
            sleep_ms(1000);
            retry_counter++;
        }
    }
    return session_state;
}