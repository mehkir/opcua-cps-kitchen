#include <signal.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <time.h>
#include "callback_scheduler.hpp"

static void callback_method(UA_Server* _server, void *data) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: called", __FUNCTION__);
}

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
    status = UA_ServerConfig_setDefault(server_config);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the server");
        return status;
    }

    callback_scheduler cs(server, callback_method, NULL, NULL);
    UA_DateTime now = UA_DateTime_nowMonotonic();
    // cs.schedule_from_now(now + UA_DATETIME_SEC);
    cs.schedule_from_now_relative(1000);

    /* Run the server loop */
    status = UA_Server_run(server, &running);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the server");
    }

    /* Clean up */
    UA_Server_delete(server);

    return 0;
}