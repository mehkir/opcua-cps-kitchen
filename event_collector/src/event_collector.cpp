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
    UA_NodeId position_node_id = node_browser_helper().get_attribute_id(remote_robot_client, ROBOT_TYPE, POSITION);
    if (UA_NodeId_equal(&position_node_id, &UA_NODEID_NULL)) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
        UA_Client_delete(remote_robot_client);
        return;
    }
    information_node_reader inr;
    if (inr.read_information_node(remote_robot_client, position_node_id) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not read the %s attribute id", __FUNCTION__, POSITION);
        UA_Client_delete(remote_robot_client);
        return;
    }
    position_t remote_robot_position = *(position_t*)inr.get_variant()->data;

    if (position_remote_robot_map_.find(remote_robot_position) != position_remote_robot_map_.end()) {
        UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d is already being monitored. Skipping ...", __FUNCTION__, remote_robot_position);
        UA_Client_delete(remote_robot_client);
        return;
    }
    position_remote_robot_map_[remote_robot_position] = std::make_unique<remote_robot>(_endpoint, remote_robot_position);
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