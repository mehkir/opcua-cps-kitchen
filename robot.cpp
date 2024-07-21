#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <signal.h>
#include <stdio.h>

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static void usage(char *program) {
    printf("Usage: %s port index\n", program);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Server *server = UA_Server_new();
    UA_ServerConfig* server_config = UA_Server_getConfig(server);
    if (argc > 2) {
        status = UA_ServerConfig_setMinimal(server_config, atoi(argv[1]), NULL);
    } else {
        usage(argv[0]);
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Please provide a port and an index");
        return UA_STATUSCODE_BAD;
    }
    
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        return status;
    }

    /* Define the attribute of the value_# variable node */
    UA_VariableAttributes variable_attributes = UA_VariableAttributes_default;
    UA_Int32 index = atoi(argv[2]);
    UA_Variant_setScalar(&variable_attributes.value, &index, &UA_TYPES[UA_TYPES_INT32]);
    variable_attributes.description = UA_LOCALIZEDTEXT("en-US", "the index");
    variable_attributes.displayName = UA_LOCALIZEDTEXT("en-US", "the index");
    variable_attributes.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    variable_attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    /* Define where the node shall be added with which browsename */
    UA_NodeId requested_new_node_id = UA_NODEID_STRING(1, "the.index");
    UA_NodeId parent_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId reference_type_id = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_QualifiedName browse_name = UA_QUALIFIEDNAME(1, "the index");
    UA_NodeId type_definition = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);

    /* Add the variable node to the information model */
    UA_Server_addVariableNode(server, requested_new_node_id,
        parent_node_id, reference_type_id, browse_name,
        type_definition, variable_attributes, NULL, NULL);

    /* Run the server loop */
    UA_StatusCode retval = UA_Server_run(server, &running);

    /* Clean up */
    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}