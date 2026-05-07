#include <signal.h>
#include <iostream>

#include "event_collector.hpp"

event_collector* event_collector_instance_;

static void stop_handler(int sig) {
    std::cout << "received ctrl-c" << std::endl;
    event_collector_instance_->stop();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    event_collector event_collector_instance;
    event_collector_instance_ = &event_collector_instance;
    event_collector_instance.start();
    return 0;
}