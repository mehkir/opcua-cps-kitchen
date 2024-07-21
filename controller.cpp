#include <stdio.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <string>

static void usage(char *program) {
    printf("Usage: %s robot_count\n", program);
}

int main(int argc, char* argv[]) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    int robot_count = 0;
    if (argc > 1) {
        robot_count = atoi(argv[1]);
    } else {
        usage(argv[0]);
        return UA_STATUSCODE_BAD;
    }

    UA_Client* clients[robot_count];
    /* Create clients and connect to them */
    for (size_t i = 0; i < robot_count; i++) {
        clients[i] = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(clients[i]));
        std::string endpoint = "opc.tcp://localhost:484" + std::to_string(i);
        status = UA_Client_connect(clients[i], endpoint.c_str());
        if(status != UA_STATUSCODE_GOOD) {
            UA_Client_delete(clients[i]);
            return status;
        }
    }

    /* Read the value attribute of the node. UA_Client_readValueAttribute is a
     * wrapper for the raw read service available as UA_Client_Service_read. */
    for (size_t i = 0; i < robot_count; i++) {
        UA_Variant value; /* Variants can hold scalar values and arrays of any type */
        UA_Variant_init(&value);
        status = UA_Client_readValueAttribute(clients[i], UA_NODEID_STRING(1, "the.index"), &value);
        if(status == UA_STATUSCODE_GOOD &&
        UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32])) {
            printf("the value is: %i\n", *(UA_Int32*)value.data);
        } else {
            printf("Reading the value attribute of node %lu failed\n", i);
        }
        UA_Variant_clear(&value);
    }
    /* Clean up */
    /* Disconnects all clients internally */
    for (int i = 0; i < robot_count; i++) {
        UA_Client_delete(clients[i]);
    }
    return status == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;
}