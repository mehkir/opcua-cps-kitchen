#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>

#include <signal.h>
#include <stdio.h>

#include "information_node_inserter.hpp"
#include "node_value_subscriber.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static void usage(char *program) {
    printf("Usage: %s port clock_port\n", program);
}

static void
clock_tick_notification_callback(UA_Client *client, UA_UInt32 subId, void *subContext,
                    UA_UInt32 monId, void *monContext, UA_DataValue *value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "clock notification callback called");
    if(UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %u", new_clock_tick);
    }
}

static void
receive_tick_ack_called(UA_Client *client, void *userdata, UA_UInt32 requestId,
    UA_CallResponse *response) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called with %u results", __FUNCTION__, response->resultsSize);
    UA_StatusCode status_code = response->responseHeader.serviceResult;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad service result", __FUNCTION__);
        return;
    }

    status_code = response->results[0].statusCode;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad status code", __FUNCTION__);
        return;
    }

    UA_Boolean tick_ack_result = *(UA_Boolean*)response->results[0].outputArguments->data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %u", tick_ack_result);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Server* server = UA_Server_new();
    UA_ServerConfig* server_config = UA_Server_getConfig(server);
    UA_Int32 clock_port = 0;
    if (argc > 2) {
        status = UA_ServerConfig_setMinimal(server_config, atoi(argv[1]), NULL);
        clock_port = atoi(argv[2]);
    } else {
        usage(argv[0]);
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Please provide a port and clock port");
        return EXIT_FAILURE;
    }

    UA_Client* clock_client = UA_Client_new();
    UA_ClientConfig *cc = UA_Client_getConfig(clock_client);
    cc->securityMode = UA_MESSAGESECURITYMODE_NONE;
    UA_ClientConfig_setDefault(cc);
    std::string clock_endpoint = "opc.tcp://localhost:" + std::to_string(clock_port);
    status = UA_Client_connect(clock_client, clock_endpoint.c_str());
    if(status != UA_STATUSCODE_GOOD) {
        UA_Client_delete(clock_client);
        return EXIT_FAILURE;
    }

    node_value_subscriber clock_tick_subscriber;
    clock_tick_subscriber.subscribe_node_value(clock_client, UA_NODEID_STRING(1, "clock_tick"), &clock_tick_notification_callback);

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the server");
        return EXIT_FAILURE;
    }

    /* Clean up */
    UA_Client_delete(clock_client);
    UA_Server_delete(server);
    return status == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}