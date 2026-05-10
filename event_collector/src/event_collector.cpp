#include "../include/event_collector.hpp"
#include <iostream>
#include "information_node_reader.hpp"

event_collector::event_collector() : stopped_(false), work_guard_(boost::asio::make_work_guard(io_context_)), steady_timer_(io_context_) {

}

event_collector::~event_collector() {
    stop();
    join_worker_thread();
}

void
event_collector::discover_agents() {
    remove_stopped_robots();
    std::vector<std::string> endpoints;
    if (discovery_util_.lookup_endpoints(endpoints) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to lookup endpoints.", __FUNCTION__);
        schedule_next_agents_discovery();
        return;
    }

    for (const std::string& endpoint : endpoints) {
        UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Discovered endpoint: %s", __FUNCTION__, endpoint.c_str());
        if (node_browser_helper().has_instance(endpoint, ROBOT_TYPE)) {
            UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Discovered robot at endpoint: %s", __FUNCTION__, endpoint.c_str());
            handle_discovered_robot(endpoint);
        }
    }
    schedule_next_agents_discovery();
}

void
event_collector::schedule_next_agents_discovery() {
    steady_timer_.expires_after(std::chrono::seconds(1));
    steady_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to schedule next agents discovery (%s)", __FUNCTION__, ec.message().c_str());
            stop();
            return;
        }
        discover_agents();
    });
}

void
event_collector::handle_discovered_robot(std::string _endpoint) {
    /* Get remote robot's position */
    UA_Client* remote_robot_client = nullptr;
    client_connection_establisher cce;
    bool connected = cce.establish_connection(remote_robot_client, _endpoint);
    if (!connected) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error establishing robot client session", __FUNCTION__);
        if (remote_robot_client != nullptr)
            UA_Client_delete(remote_robot_client);
        return;
    }

    if (position_remote_robot_map_.find(_endpoint) != position_remote_robot_map_.end()) {
        UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Robot at endpoint %s is already being monitored. Skipping ...", __FUNCTION__, _endpoint.c_str());
        UA_Client_delete(remote_robot_client);
        return;
    }

    std::unique_ptr<remote_robot> robot = std::make_unique<remote_robot>(_endpoint);
    if (robot->initialize_and_start() == UA_STATUSCODE_GOOD) {
        position_remote_robot_map_[_endpoint] = std::move(robot);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot client initialitation/start failed", __FUNCTION__);
        return;
    }
}

void
event_collector::remove_stopped_robots() {
    // UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (auto it = position_remote_robot_map_.begin(); it != position_remote_robot_map_.end();) {
        if (it->second->is_stopped()) {
            std::string endpoint = it->first;
            it = position_remote_robot_map_.erase(it);
            UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "Removed remote robot at endpoint %s", endpoint.c_str());
        } else {
            it++;
        }
    }
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
    discover_agents();
    join_worker_thread();
    UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Exited start method", __FUNCTION__);
}

void
event_collector::stop() {
    if (stopped_.load()) {
        UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Event collector is already stopped", __FUNCTION__);
        return;
    }
    work_guard_.reset();
    io_context_.stop();
    discovery_util_.stop();
    stopped_.store(true);
    UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Stopped successfully", __FUNCTION__);
}