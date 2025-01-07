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
    
    // _position, port, _controller_port, _conveyor_port
    robot robot_instance(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
    robot_instance_ = &robot_instance;
    robot_instance.start();
    return 0;
}