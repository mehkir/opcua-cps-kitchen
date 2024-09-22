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
    client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    UA_ClientConfig_setDefault(client_config);
    std::string server_endpoint = "opc.tcp://localhost:" + std::to_string(_server_port);

    /* Connect to a server */
    UA_SessionState session_state;
    UA_SecureChannelState secure_channel_state;
    int retry_counter = 0;
    UA_Client_connectAsync(_client, server_endpoint.c_str());
    do {
        UA_Client_run_iterate(_client, 0);
        UA_Client_getState(_client, &secure_channel_state, &session_state, NULL);
        if (secure_channel_state == UA_SECURECHANNELSTATE_CLOSED) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Retrying to connect to the server");
            UA_Client_connectAsync(_client, server_endpoint.c_str());
            sleep_ms(1000);
            retry_counter++;
        }
    } while (session_state != UA_SESSIONSTATE_ACTIVATED && retry_counter < RETRY_LIMIT);
    return session_state;
}