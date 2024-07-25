#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <signal.h>
#include <stdio.h>
#include <set>
#include "information_node_inserter.hpp"

std::set<robot> acknowledged_robots;

struct robot {
    private:
        UA_UInt32 robot_id_;
    public:
        robot(UA_UInt32 _robot_id) : robot_id_(_robot_id) {
        }

        ~robot() {
        }
};

static volatile UA_Boolean running = true;
static void stop_handler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static void usage(char *program) {
    printf("Usage: %s port robot_count\n", program);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

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

    UA_UInt64 clock_tick = 0;
    information_node_inserter info_node_inserter;
    info_node_inserter.add_information_node(server, "clock_tick", "the clock tick", UA_TYPES_UINT64, &clock_tick);

    /* Run the server loop */
    UA_StatusCode retval = UA_Server_run(server, &running);

    /* Clean up */
    UA_Server_delete(server);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}