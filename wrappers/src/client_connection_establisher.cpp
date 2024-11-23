#include "../include/client_connection_establisher.hpp"
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <string>
#include <unistd.h>

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
    std::string server_endpoint = "opc.tcp://localhost:" + std::to_string(_server_port);

    /* Connect to a server */
    UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
    int retry_counter = 0;
    while(retry_counter < RETRY_LIMIT) {
        UA_StatusCode status_code = UA_Client_connect(_client, server_endpoint.c_str());
        if(status_code != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Not connected. Retrying to connect in 1 second");
            sleep_ms(1000);
            retry_counter++;
            continue;
        }
        session_state = UA_SESSIONSTATE_ACTIVATED;
        break;
    }
    return session_state;
}