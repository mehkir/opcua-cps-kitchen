#include "information_node_reader.hpp"
#include "client_connection_establisher.hpp"
#include "node_value_subscriber.hpp"

#include <string>
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <signal.h>


static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}

static void string_changed(UA_Client* _client, UA_UInt32 _sub_id, void *sub_context,
    UA_UInt32 mon_id, void* mon_context, UA_DataValue* value) {
        UA_UInt32 sample_data = *(UA_UInt32*) mon_context;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Monitor context: %d", sample_data);
        UA_String new_string = *(UA_String*) value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "New string value: %s", std::string((char*) new_string.data, new_string.length).c_str());
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_Client* client = UA_Client_new();
    client_connection_establisher con_estab;
    con_estab.establish_connection(client, 4840);

    UA_UInt32 sample_data = 12345;

    node_value_subscriber string_subscriber;
    string_subscriber.subscribe_node_value(client, UA_NODEID_STRING(1, const_cast<char*>("str_node")), string_changed, &sample_data);

    while(running) {
        if (UA_Client_run_iterate(client, 1000) != UA_STATUSCODE_GOOD) {
            running = false;
        }
    }
    return 0;
}