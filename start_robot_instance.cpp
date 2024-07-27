#include <robot.hpp>
#include <signal.h>

robot* robot_instance_;

static void stop_handler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    robot_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    
    robot robot_instance(atoi(argv[1]), atoi(argv[2]));
    robot_instance_ = &robot_instance;
    robot_instance.start();
    return 0;
}