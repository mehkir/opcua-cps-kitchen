#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>

#include <signal.h>
#include <stdio.h>
#include <set>
#include <unordered_map>

#include "information_node_inserter.hpp"
#include "method_node_inserter.hpp"

UA_Int32 robot_count_ = 0;
UA_UInt64 next_clock_tick_ = 0;
std::set<uint32_t> currently_acknowledged_set;

static UA_StatusCode
receive_tick_ack (UA_Server *server,
        const UA_NodeId *session_id, void *session_context,
        const UA_NodeId *method_id, void *method_context,
        const UA_NodeId *object_id, void *object_context,
        size_t input_size, const UA_Variant *input,
        size_t output_size, UA_Variant *output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "receive_tick_ack called");
    UA_UInt32 port = *(UA_UInt32*)input[0].data;
    UA_UInt32 next_tick = *(UA_UInt32*)input[1].data;
    currently_acknowledged_set.insert(port);
    if(currently_acknowledged_set.size() != robot_count_) {
        if (currently_acknowledged_set.size() == 1) {
            next_clock_tick_ = next_tick;
        } else {
            next_clock_tick_ = next_tick < next_clock_tick_ ? next_tick : next_clock_tick_;
        }
    } else {
        UA_Variant new_clock_tick;
        UA_Variant_setScalar(&new_clock_tick, &next_clock_tick_, &UA_TYPES[UA_TYPES_UINT64]);
        currently_acknowledged_set.clear();
        UA_Server_writeValue(server, UA_NODEID_STRING(1, "clock_tick"), new_clock_tick);
    }
    UA_Boolean ack_received = true;
    UA_Variant_setScalarCopy(output, &ack_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    return UA_STATUSCODE_GOOD;
}

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
        robot_count_ = atoi(argv[2]);
    } else {
        usage(argv[0]);
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Please provide a port and an index");
        return EXIT_FAILURE;
    }
    
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        return EXIT_FAILURE;
    }

    UA_UInt64 clock_tick = 0;
    information_node_inserter info_node_inserter;
    status = info_node_inserter.add_information_node(server, UA_NODEID_STRING(1, "clock_tick"), "the clock tick", UA_TYPES_UINT64, &clock_tick);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error adding information node");
        return EXIT_FAILURE;
    }

    method_node_inserter receive_tick_ack_inserter;
    receive_tick_ack_inserter.add_input_argument("port of the tick client", "port", UA_TYPES_UINT16);
    receive_tick_ack_inserter.add_input_argument("next tick of the tick client", "next_tick", UA_TYPES_UINT64);
    receive_tick_ack_inserter.add_output_argument("ack received", "ack_received", UA_TYPES_BOOLEAN);
    status = receive_tick_ack_inserter.add_method_node(server, UA_NODEID_STRING(1,"receive_tick_ack"), "receive tick ack", &receive_tick_ack);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error adding method node");
        return EXIT_FAILURE;
    }

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the server");
        return EXIT_FAILURE;
    }

    /* Clean up */
    status = UA_Server_delete(server);
    return status == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}