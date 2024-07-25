#include <stdio.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <string>
#include "information_node_reader.hpp"

static void usage(char *program) {
    printf("Usage: %s start_port robot_count\n", program);
}

int main(int argc, char* argv[]) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    int start_port = 0;
    int robot_count = 0;
    if (argc > 2) {
        start_port = atoi(argv[1]);
        robot_count = atoi(argv[2]);
    } else {
        usage(argv[0]);
        return UA_STATUSCODE_BAD;
    }

    UA_Client* clients[robot_count];
    /* Create clients and connect to them */
    for (size_t i = 0; i < robot_count; i++) {
        clients[i] = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(clients[i]));
        int remote_port = start_port + i;
        std::string endpoint = "opc.tcp://localhost:" + std::to_string(remote_port);
        status = UA_Client_connect(clients[i], endpoint.c_str());
        if(status != UA_STATUSCODE_GOOD) {
            UA_Client_delete(clients[i]);
        }
    }

    if (status != UA_STATUSCODE_GOOD) {
        return status;
    }

    /* Read the value attribute of the node. UA_Client_readValueAttribute is a
     * wrapper for the raw read service available as UA_Client_Service_read. */
    for (size_t i = 0; i < robot_count; i++) {
        information_node_reader in_reader;
        status = in_reader.read_information_node(clients[i], UA_NODEID_STRING(1, "the.index"));
        if(status == UA_STATUSCODE_GOOD &&
        UA_Variant_hasScalarType(in_reader.get_variant(), &UA_TYPES[UA_TYPES_INT32])) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "the value is: %i", *(UA_Int32*)in_reader.get_variant()->data);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Reading the value attribute of node %lu failed", i);
        }
    }
    /* Clean up */
    /* Disconnects all clients internally */
    for (int i = 0; i < robot_count; i++) {
        UA_Client_delete(clients[i]);
    }
    return status == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}