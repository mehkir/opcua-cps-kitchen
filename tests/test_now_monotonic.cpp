#include <signal.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <time.h>
#include <thread>
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
        return EXIT_FAILURE;
    }
    status = UA_Server_run_startup(server);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at server startup", __FUNCTION__);
        running = false;
        return EXIT_FAILURE;
    }
    std::thread server_iterate_thread;
    /* Start the server eventloop */
    try {
        server_iterate_thread = std::thread([server]() {
            while(running) {
                UA_Server_run_iterate(server, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running server", __FUNCTION__);
        running = false;
        return EXIT_FAILURE;
    }

    callback_scheduler cs(server, callback_method, NULL, NULL);
    UA_DateTime now = UA_DateTime_nowMonotonic();
    cs.schedule_from_now(now + 5*UA_DATETIME_SEC);
    UA_DateTime scheduled = UA_DateTime_now() + (10*UA_DATETIME_SEC);
    UA_StatusCode cb_status = UA_Server_addTimedCallback(server, callback_method, NULL, scheduled, NULL);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Scheduled callback at %" PRIu64 ", now is %" PRIu64 ", status: 0x%08x", scheduled, UA_DateTime_now(), cb_status);
    cs.schedule_from_now_relative(1000);

    if (server_iterate_thread.joinable())
        server_iterate_thread.join();

    /* Stop server */
    UA_Server_run_shutdown(server);
    /* Clean up */
    UA_Server_delete(server);
    return 0;
}