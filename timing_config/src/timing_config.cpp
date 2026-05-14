#include "../include/timing_config.hpp"

#include <jsoncpp/json/json.h>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
#include <limits.h>
#include <filesystem>
#include <iostream>

timing_config* timing_config::instance_ = nullptr;
std::mutex timing_config::mutex_;

timing_config*
timing_config::get_instance() {
    std::lock_guard<std::mutex> lockguard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new timing_config();
    }
    return instance_;
}

timing_config::timing_config(/* args */) {
    load_timing_config();
}


timing_config::~timing_config() {
}

void
timing_config::load_timing_config() {
    char buffer[PATH_MAX + 1];  // +1 for the null terminator
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len == -1) {
        perror("readlink");
        return;
    }
    buffer[len] = '\0';  // null terminate
    std::filesystem::path exe_path(buffer);
    std::filesystem::path exe_dir = exe_path.parent_path();
    std::filesystem::path timing_config_path = exe_dir.parent_path() / "timing_config.json";
    std::ifstream ifs_timing_config(timing_config_path.string());
    Json::Value agent_times;
    Json::Reader reader;
    if (!reader.parse(ifs_timing_config, agent_times)) {
        std::cerr << reader.getFormattedErrorMessages() << std::endl;
    }
    for (auto agent_name : agent_times.getMemberNames()) {
        std::unordered_map<std::string, duration_t> agent_timing_map;
        for (auto timing_name : agent_times[agent_name].getMemberNames()) {
            agent_timing_map.emplace(timing_name, agent_times[agent_name][timing_name].asUInt64());
        }
        timing_map_.emplace(agent_name, agent_timing_map);
    }
}

duration_t
timing_config::get_timing(const std::string _agent_name, const std::string _timing_name) const {
    return timing_map_.at(_agent_name).at(_timing_name);
}