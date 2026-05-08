#include "../include/event_collector.hpp"
#include <iostream>

event_collector::event_collector() : work_guard_(boost::asio::make_work_guard(io_context_)) {

}

event_collector::~event_collector() {
    stop();
    join_worker_thread();
}

void
event_collector::discover_robots() {
    
}

void
event_collector::join_worker_thread() {
    if (worker_thread_.joinable())
        worker_thread_.join();
}

void
event_collector::start() {
    if (!worker_thread_.joinable()) {
        worker_thread_ = std::thread([this]() {
            io_context_.run();
            UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Exited io_context", __FUNCTION__);
        });
    }
    join_worker_thread();
    UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Exited start method", __FUNCTION__);
}

void
event_collector::stop() {
    work_guard_.reset();
    UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Stopped successfully", __FUNCTION__);
}