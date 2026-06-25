#include "../include/event_collector.hpp"
#include <iostream>
#include "information_node_reader.hpp"
#include <limits.h>
#include <system_error>

#define TIMESTAMP_DIR "timestamp_results"

event_collector::event_collector() : stopped_(false), work_guard_(boost::asio::make_work_guard(io_context_)), steady_timer_(io_context_), timestamp_dir_(get_project_dir() / TIMESTAMP_DIR / get_timestamp_dir_name().str()) {
    std::error_code ec;
    if (!std::filesystem::exists(timestamp_dir_, ec)) {
        std::filesystem::create_directories(timestamp_dir_);
    }
    if (ec) {
        std::cerr << "Error creating timestamp directory: " << ec.message() << std::endl;
    }
}

event_collector::~event_collector() {
    stop();
    join_worker_thread();

    remote_robot_map_.clear();
    remote_kitchen_.reset();

    UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Destructor finished successfully", __FUNCTION__);
}

void
event_collector::discover_agents() {
    remove_stopped_robots();
    remove_stopped_kitchen();
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
        } else if (node_browser_helper().has_instance(endpoint, KITCHEN_TYPE)) {
            UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Discovered kitchen at endpoint: %s", __FUNCTION__, endpoint.c_str());
            handle_discovered_kitchen(endpoint);
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
    if (remote_robot_map_.find(_endpoint) != remote_robot_map_.end()) {
        UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Robot at endpoint %s is already being monitored. Skipping ...", __FUNCTION__, _endpoint.c_str());
        return;
    }

    std::unique_ptr<remote_robot> robot = std::make_unique<remote_robot>(_endpoint, timestamp_dir_);
    if (robot->initialize_and_start() == UA_STATUSCODE_GOOD) {
        remote_robot_map_[_endpoint] = std::move(robot);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot client initialization/start failed", __FUNCTION__);
        return;
    }
}

void
event_collector::handle_discovered_kitchen(std::string _endpoint) {
    if (remote_kitchen_) {
        UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Kitchen at endpoint %s is already being monitored. Skipping ...", __FUNCTION__, _endpoint.c_str());
        return;
    }

    std::unique_ptr<remote_kitchen> kitchen = std::make_unique<remote_kitchen>(_endpoint, timestamp_dir_);
    if (kitchen->initialize_and_start() == UA_STATUSCODE_GOOD) {
        remote_kitchen_ = std::move(kitchen);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Kitchen client initialization/start failed", __FUNCTION__);
        return;
    }
}

void
event_collector::remove_stopped_robots() {
    // UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (auto it = remote_robot_map_.begin(); it != remote_robot_map_.end();) {
        if (it->second->is_stopped()) {
            std::string endpoint = it->first;
            it = remote_robot_map_.erase(it);
            UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "Removed remote robot at endpoint %s", endpoint.c_str());
        } else {
            it++;
        }
    }
}

void
event_collector::remove_stopped_kitchen() {
    if (remote_kitchen_ && remote_kitchen_->is_stopped()) {
        remote_kitchen_.reset();
    }
}

void
event_collector::join_worker_thread() {
    if (worker_thread_.joinable())
        worker_thread_.join();
}

std::filesystem::path
event_collector::get_project_dir() const {
    char directory_buffer[PATH_MAX + 1];  // +1 for the null terminator
    ssize_t len = readlink("/proc/self/exe", directory_buffer, sizeof(directory_buffer) - 1);
    if (len == -1) {
        perror("readlink");
        return std::filesystem::path();
    }
    directory_buffer[len] = '\0';  // null terminate
    std::filesystem::path exe_path(directory_buffer);
    std::filesystem::path build_dir = exe_path.parent_path();
    std::filesystem::path project_dir = build_dir.parent_path();
    return project_dir;
}

std::ostringstream
event_collector::get_timestamp_dir_name() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm{};
    localtime_r(&now_time, &local_tm);   // Linux/POSIX thread-safe localtime

    std::ostringstream date_stream;
    date_stream << std::put_time(&local_tm, "%Y-%m-%d");
    return date_stream;
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
    if (stopped_.exchange(true)) {
        UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Event collector is already stopped", __FUNCTION__);
        return;
    }
    work_guard_.reset();
    io_context_.stop();
    discovery_util_.stop();
    UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Stopped successfully", __FUNCTION__);
}