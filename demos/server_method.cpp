#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <signal.h>
#include "method_node_inserter.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static
UA_StatusCode method_callback(UA_Server *server, const UA_NodeId *sessionId, void *sessionContext,
                            const UA_NodeId *methodId, void *methodContext,
                            const UA_NodeId *objectId, void *objectContext,
                            size_t inputSize, const UA_Variant *input,
                            size_t outputSize, UA_Variant *output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: called", __FUNCTION__);
    UA_Boolean result = true;
    UA_Variant_setScalarCopy(output, &result, &UA_TYPES[UA_TYPES_BOOLEAN]);
    return UA_STATUSCODE_GOOD;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Server* server = UA_Server_new();
    UA_ServerConfig* server_config = UA_Server_getConfig(server);
    status = UA_ServerConfig_setDefault(server_config);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the server");
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    method_node_inserter mni;
    mni.add_output_argument("output argument", "output_argument", UA_TYPES_BOOLEAN);
    mni.add_method_node(server, UA_NODEID_STRING(0, const_cast<char*>("server_method")), "server_method", method_callback, NULL);

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the server");
        UA_Server_delete(server);
        return EXIT_FAILURE;
    }

    /* Clean up */
    UA_Server_delete(server);

    return EXIT_SUCCESS;
}