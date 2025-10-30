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
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << "<position> <capabilities_file_name> <conveyor_size>" << std::endl;
        return 0;
    }
    robot robot_instance(atoi(argv[1]), argv[2], atoi(argv[3]));
    robot_instance_ = &robot_instance;
    robot_instance.start();
    return 0;
}