#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <signal.h>
#include <set>

#include "information_node_inserter.hpp"
#include "method_node_inserter.hpp"

UA_UInt64 clock_tick_ = 0;
UA_UInt64 next_clock_tick_ = 0;
UA_Int32 clock_client_count_ = 0;
std::set<uint16_t> currently_acknowledged_set_;

static UA_StatusCode
receive_tick_ack (UA_Server *server,
        const UA_NodeId *session_id, void *session_context,
        const UA_NodeId *method_id, void *method_context,
        const UA_NodeId *object_id, void *object_context,
        size_t input_size, const UA_Variant *input,
        size_t output_size, UA_Variant *output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Extract input arguments */
    UA_UInt16 port = *(UA_UInt16*)input[0].data;
    UA_UInt64 current_client_tick = *(UA_UInt64*)input[1].data;
    UA_UInt64 next_tick = *(UA_UInt64*)input[2].data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Extracted inputs: port: %d, current_client_tick: %lu, next_tick: %lu", port, current_client_tick, next_tick);
    /* Check if the current tick of the client is equal to the current tick of the server */
    if (current_client_tick != clock_tick_) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "current tick of the client is not equal to the current tick of the server");
        UA_Boolean ack_received = false;
        UA_Variant_setScalarCopy(output, &ack_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
        return UA_STATUSCODE_GOOD;
    }
    /* Determine the next smallest tick and if all clients sent their next ticks */
    if(currently_acknowledged_set_.size() != clock_client_count_) {
        if (next_tick > clock_tick_) {
            if (next_clock_tick_ == clock_tick_) {
                next_clock_tick_ = next_tick;
            } else {
                next_clock_tick_ = next_tick < next_clock_tick_ ? next_tick : next_clock_tick_;
            }
        }
        currently_acknowledged_set_.insert(port);
        UA_Boolean ack_received = true;
        UA_Variant_setScalarCopy(output, &ack_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Ack received from port: %d", port);
    } 
    if (currently_acknowledged_set_.size() == clock_client_count_) {
        UA_Variant new_clock_tick;
        UA_Variant_setScalar(&new_clock_tick, &next_clock_tick_, &UA_TYPES[UA_TYPES_UINT64]);
        currently_acknowledged_set_.clear();
        clock_tick_ = next_clock_tick_;
        UA_Boolean ack_received = true;
        UA_Variant_setScalarCopy(output, &ack_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_Server_writeValue(server, UA_NODEID_STRING(1, const_cast<char*>("clock_tick")), new_clock_tick);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "New clock tick is: %lu", clock_tick_);
    }
    return UA_STATUSCODE_GOOD;
}

static volatile UA_Boolean running = true;
static void stop_handler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static void usage(char *program) {
    printf("Usage: %s port clock_client_count\n", program);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Server *server = UA_Server_new();
    UA_ServerConfig* server_config = UA_Server_getConfig(server);
    if (argc > 2) {
        status = UA_ServerConfig_setMinimal(server_config, atoi(argv[1]), NULL);
        clock_client_count_ = atoi(argv[2]);
    } else {
        usage(argv[0]);
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Please provide a port and an index");
        return EXIT_FAILURE;
    }
    
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the server");
        return EXIT_FAILURE;
    }

    information_node_inserter info_node_inserter;
    status = info_node_inserter.add_scalar_node(server, UA_NODEID_STRING(1, const_cast<char*>("clock_tick")), "the clock tick", UA_TYPES_UINT64, &clock_tick_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding information node");
        return EXIT_FAILURE;
    }

    method_node_inserter receive_tick_ack_inserter;
    receive_tick_ack_inserter.add_input_argument("port of the tick client", "port", UA_TYPES_UINT16);
    receive_tick_ack_inserter.add_input_argument("current tick of the client", "current_client_tick", UA_TYPES_UINT64);
    receive_tick_ack_inserter.add_input_argument("next tick of the tick client", "next_tick", UA_TYPES_UINT64);
    receive_tick_ack_inserter.add_output_argument("ack received", "ack_received", UA_TYPES_BOOLEAN);
    status = receive_tick_ack_inserter.add_method_node(server, UA_NODEID_STRING(1, const_cast<char*>("receive_tick_ack")), "receive tick ack", receive_tick_ack);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding method node");
        return EXIT_FAILURE;
    }

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the server");
        return EXIT_FAILURE;
    }

    /* Clean up */
    UA_Server_delete(server);
    return status == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}