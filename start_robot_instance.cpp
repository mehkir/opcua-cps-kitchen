#include <signal.h>
#include <iostream>

#include "robot.hpp"

robot* robot_instance_;

static void stop_handler(int sig) {
    std::cout << "received ctrl-c" << std::endl;
    robot_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    
    // _position
    if (argc < 2) {
        std::cout << "Provide robot position" << std::endl;
        return 0;
    }
    robot robot_instance(atoi(argv[1]));
    robot_instance_ = &robot_instance;
    robot_instance.start();
    return 0;
}