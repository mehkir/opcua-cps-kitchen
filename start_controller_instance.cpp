#include <signal.h>
#include <open62541/plugin/log_stdout.h>
#include "controller.hpp"

controller* controller_instance_;

static void stop_handler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    controller_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    // _controller_port, _robot_start_port, _robot_count, _conveyor_start_port, _conveyor_count _clock_port
    controller controller_instance(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]));
    controller_instance_ = &controller_instance;
    controller_instance.start();
    return 0;
}