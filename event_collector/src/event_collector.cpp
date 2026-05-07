#include "../include/event_collector.hpp"
#include <iostream>

event_collector::event_collector() : work_guard_(boost::asio::make_work_guard(io_context_)) {

}

event_collector::~event_collector() {
    stop();
    if (worker_thread_.joinable())
        worker_thread_.join();
}

void
event_collector::discover_robots() {
    
}

void
event_collector::start() {
    if (!worker_thread_.joinable()) {
        worker_thread_ = std::thread([this]() {
            io_context_.run();
            std::cout << "Event Collector: Exited io_context" << std::endl;
        });
    }
}

void
event_collector::stop() {
    if (worker_thread_.joinable())
        work_guard_.reset();
    std::cout << "Event Collector: Stopped successfully" << std::endl;  
}