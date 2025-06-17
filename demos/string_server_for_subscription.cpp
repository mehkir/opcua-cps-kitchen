#include <signal.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include "information_node_inserter.hpp"
#include "information_node_writer.hpp"
#include "information_node_reader.hpp"
#include "callback_scheduler.hpp"
#include "filtered_logger.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static void change_string(UA_Server* _server, void *data) {
    static uint32_t counter = 0;
    information_node_reader str_value_reader;
    UA_StatusCode status = str_value_reader.read_information_node(_server, UA_NODEID_STRING(1, const_cast<char*>("str_node")));
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with reading string value");
        running = false;
    }
    UA_String string_value = *(UA_String*) str_value_reader.get_variant()->data;
    std::string new_str = std::string((char*) string_value.data, string_value.length) + ", " + std::to_string(counter);
    UA_String new_string_value = UA_STRING(const_cast<char*>(new_str.c_str()));
    information_node_writer str_writer;
    str_writer.write_value(_server, UA_NODEID_STRING(1, const_cast<char*>("str_node")), &new_string_value, &UA_TYPES[UA_TYPES_STRING]);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "New string value is: %s", new_str.c_str());
    counter++;
    callback_scheduler change_str_information_node(_server, change_string, NULL, NULL);
    status = change_str_information_node.schedule_from_now_relative(1000);
    if (status != UA_STATUSCODE_GOOD) {
        running = false;
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error setting callback scheduler");
    }
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
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        return status;
    }
    *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);

    UA_String string_value = UA_STRING(const_cast<char*>("I got a story to tell and this is it"));
    information_node_inserter str_inserter;
    status = str_inserter.add_scalar_node(server, UA_NODEID_STRING(1, const_cast<char*>("str_node")), "str node", UA_TYPES_STRING, &string_value);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error adding string value information node");
        running = false;
    }

    callback_scheduler change_str_information_node(server, change_string, NULL, NULL);
    status = change_str_information_node.schedule_from_now_relative(1000);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error setting callback scheduler");
        running = false;
    }

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the server");
    }

    /* Clean up */
    UA_Server_delete(server);

    return 0;
}