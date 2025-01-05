#include "../include/capability_actions.hpp"

capability_actions* capability_actions::instance_;
std::mutex capability_actions::mutex_;

capability_actions* capability_actions::get_instance() {
    std::lock_guard<std::mutex> lockguard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new capability_actions();
    }
    return instance_;
}

capability_actions::capability_actions() {
}

capability_actions::~capability_actions() {
}