#include <signal.h>
#include <iostream>

#include "kitchen.hpp"

kitchen* kitchen_instance_;

static void stop_handler(int sig) {
    std::cout << "received ctrl-c" << std::endl;
    kitchen_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << "<robots_count> [<evaluate_orders_count>]" << std::endl;
        return 0;
    }
    uint32_t evaluate_orders_count = 0;
    if (argc > 2)
        evaluate_orders_count = atoi(argv[2]);
    kitchen kitchen_instance(atoi(argv[1]), evaluate_orders_count);
    kitchen_instance_ = &kitchen_instance;
    kitchen_instance.start();
    return 0;
}