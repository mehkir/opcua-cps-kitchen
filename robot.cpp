#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <signal.h>
#include <stdio.h>
#include "information_node_inserter.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static void usage(char *program) {
    printf("Usage: %s index\n", program);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Server *server = UA_Server_new();
    UA_ServerConfig* server_config = UA_Server_getConfig(server);
    UA_Int32 index = atoi(argv[1]);
    if (argc > 1) {
        status = UA_ServerConfig_setMinimal(server_config, 4840+index, NULL);
    } else {
        usage(argv[0]);
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Please provide a port and an index");
        return UA_STATUSCODE_BAD;
    }
    
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        return status;
    }

    information_node_inserter info_node_manager;
    info_node_manager.add_information_node(server, "the.index", "the index", UA_TYPES_INT32, &index);

    /* Run the server loop */
    UA_StatusCode retval = UA_Server_run(server, &running);

    /* Clean up */
    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}