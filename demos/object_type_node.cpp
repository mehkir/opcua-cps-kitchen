#include <signal.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include "object_type_node_inserter.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Server* server = UA_Server_new();
    UA_ServerConfig* server_config = UA_Server_getConfig(server);
    if (argc > 1) {
        status = UA_ServerConfig_setMinimal(server_config, atoi(argv[1]), NULL);
    } else {
        status = UA_ServerConfig_setDefault(server_config);
    }
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the server");
        return status;
    }

    object_type_node_inserter type_node_inserter(server,"RobotType");
    type_node_inserter.add_attribute("RobotType", "action");
    type_node_inserter.add_object_type_constructor(server, type_node_inserter.get_object_type_id("RobotType"));
        
    type_node_inserter.add_object_sub_type("CookingRobotType");
    type_node_inserter.add_attribute("CookingRobotType", "model");
    type_node_inserter.add_attribute("CookingRobotType", "arrayFeature");
    type_node_inserter.add_object_type_constructor(server, type_node_inserter.get_object_type_id("CookingRobotType"));
    type_node_inserter.add_object_instance("CookingRobot 1", "CookingRobotType");

    UA_String action_name = UA_STRING(const_cast<char*>("whip the action"));
    type_node_inserter.set_scalar_attribute("CookingRobot 1", "action", &action_name, UA_TYPES_STRING);
    UA_String model_name = UA_STRING(const_cast<char*>("Cookomatic 3000"));
    type_node_inserter.set_scalar_attribute("CookingRobot 1", "model", &model_name, UA_TYPES_STRING);

    std::string values[] = {"Hello", "World", "!"};
    UA_String* capabilities;
    capabilities = (UA_String*) UA_Array_new(3, &UA_TYPES[UA_TYPES_STRING]);
    for (int i = 0; i < 3; i++) {
        capabilities[i] = UA_STRING(const_cast<char*>(values[i].c_str()));
    }
    type_node_inserter.set_array_attribute("CookingRobot 1", "arrayFeature", capabilities, 3, UA_TYPES_STRING);


    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the server");
    }

    /* Clean up */
    UA_Server_delete(server);

    return 0;
}