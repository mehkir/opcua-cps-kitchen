#include <signal.h>
#include <iostream>

#include "conveyor.hpp"

conveyor* conveyor_instance_;

static void stop_handler(int sig) {
    std::cout << "received ctrl-c" << std::endl;
    conveyor_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << "<robots_count>" << std::endl;
        return 0;
    }

    conveyor conveyor_instance(atoi(argv[1]));
    conveyor_instance_ = &conveyor_instance;
    conveyor_instance.start();
    return 0;
}