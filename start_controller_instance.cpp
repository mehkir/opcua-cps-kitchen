#include <signal.h>
#include <iostream>

#include "controller.hpp"
#include "kitchen_mape.hpp"

controller* controller_instance_;

static void stop_handler(int sig) {
    std::cout << "received ctrl-c" << std::endl;
    controller_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    controller controller_instance(std::make_unique<kitchen_mape>());
    controller_instance_ = &controller_instance;
    controller_instance.start();
    return 0;
}