#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include <open62541/client_highlevel_async.h>

#include <signal.h>
#include <stdio.h>
#include <thread>
#include <time.h>

#include "information_node_inserter.hpp"
#include "node_value_subscriber.hpp"

UA_Int64 current_clock_tick_ = 0;
UA_Int64 next_clock_tick_ = 0;

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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);
        current_clock_tick_ = new_clock_tick;
    }
}

static void
receive_tick_ack_called(UA_Client *client, void *userdata, UA_UInt32 requestId,
    UA_CallResponse *response) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called with %lu results", __FUNCTION__, response->resultsSize);
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

    if(UA_Variant_hasScalarType(response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_Boolean tick_ack_result = *(UA_Boolean*)response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, tick_ack_result);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Server* server = UA_Server_new();
    UA_ServerConfig* server_config = UA_Server_getConfig(server);
    UA_UInt16 robot_port = 0;
    UA_UInt16 clock_port = 0;
    if (argc > 2) {
        robot_port = atoi(argv[1]);
        clock_port = atoi(argv[2]);
        status = UA_ServerConfig_setMinimal(server_config, atoi(argv[1]), NULL);
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
    clock_tick_subscriber.subscribe_node_value(clock_client, UA_NODEID_STRING(1, const_cast<char*>("clock_tick")), clock_tick_notification_callback, NULL);

    srand(time(NULL));
    next_clock_tick_ = rand() % 1000;
    UA_Variant inputs[3];
    UA_Variant_init(&inputs[0]);
    UA_Variant_setScalar(&inputs[0], &robot_port, &UA_TYPES[UA_TYPES_UINT16]);
    UA_Variant_init(&inputs[1]);
    UA_Variant_setScalar(&inputs[1], &current_clock_tick_, &UA_TYPES[UA_TYPES_UINT64]);
    UA_Variant_init(&inputs[2]);
    UA_Variant_setScalar(&inputs[2], &next_clock_tick_, &UA_TYPES[UA_TYPES_UINT64]);
    UA_UInt32 request_id = 0;
    UA_Client_call_async(clock_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(1, const_cast<char*>("receive_tick_ack")), 3, inputs, receive_tick_ack_called, NULL, &request_id);

    /* Run client iterate asynchronously */
    std::thread client_iterate_thread([clock_client]() {
        while(running) {
            UA_Client_run_iterate(clock_client, 1000);
        }
    });

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the server");
        return EXIT_FAILURE;
    }

    client_iterate_thread.join();
    /* Clean up */
    UA_Client_delete(clock_client);
    UA_Server_delete(server);
    return status == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}