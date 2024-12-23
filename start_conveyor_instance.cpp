#include "conveyor.hpp"
#include <signal.h>
#include <open62541/plugin/log_stdout.h>

conveyor* conveyor_instance_;

static void stop_handler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    conveyor_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    
    // _conveyor_port, _robot_count
    conveyor conveyor_instance(atoi(argv[1]), atoi(argv[2]));
    conveyor_instance_ = &conveyor_instance;
    conveyor_instance.start();
    return 0;
}