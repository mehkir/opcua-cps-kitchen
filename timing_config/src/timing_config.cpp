#include "../include/timing_config.hpp"

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
}

timing_config::~timing_config() {
}


duration_t timing_config::get_timing(const std::string _agent_name, const std::string _timing_name) const {
    return timing_map_.at(_agent_name).at(_timing_name);
}