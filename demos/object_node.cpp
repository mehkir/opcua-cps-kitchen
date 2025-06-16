#include <signal.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include "information_node_inserter.hpp"
#include "information_node_writer.hpp"
#include "information_node_reader.hpp"
#include "callback_scheduler.hpp"

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
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        return status;
    }

    UA_NodeId pumpId; /* get the nodeid assigned by the server */
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("Pump (Manual)"));
    UA_Server_addObjectNode(server, UA_NODEID_NULL,
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, const_cast<char*>("Pump (Manual)")),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
        oAttr, NULL, &pumpId);
    
    UA_VariableAttributes mnAttr = UA_VariableAttributes_default;
    UA_String manufacturerName = UA_STRING(const_cast<char*>("Pump King Ltd."));
    UA_Variant_setScalar(&mnAttr.value, &manufacturerName, &UA_TYPES[UA_TYPES_STRING]);
    mnAttr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("ManufacturerName"));
    UA_Server_addVariableNode(server, UA_NODEID_NULL, pumpId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, const_cast<char*>("ManufacturerName")),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        mnAttr, NULL, NULL);

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the server");
    }

    /* Clean up */
    UA_Server_delete(server);

    return 0;
}