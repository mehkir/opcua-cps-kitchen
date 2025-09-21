#include <string>
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <signal.h>

#include "client_connection_establisher.hpp"
#include "method_node_caller.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_Client* client = nullptr;
    client_connection_establisher con_estab;
    std::string server_endpoint = "opc.tcp://localhost:" + std::to_string(4840);
    bool connected = con_estab.establish_connection(client, server_endpoint.c_str());
    if (!connected) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Connecting to server endpoint %s failed", __FUNCTION__, server_endpoint.c_str());
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }

    method_node_caller mnc;
    UA_Variant* output;
    size_t output_size;
    UA_StatusCode status = mnc.call_method_node(client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(0,const_cast<char*>("server_method")), &output_size, &output);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Calling server method failed (%s)", __FUNCTION__, UA_StatusCode_name(status));
        UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        UA_Client_delete(client);
        return EXIT_FAILURE;
    }
    UA_Boolean result = *(UA_Boolean*)output->data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method returned %s", __FUNCTION__, (result ? "true" : "false"));
    UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);

    UA_Client_delete(client);
    return EXIT_SUCCESS;
}