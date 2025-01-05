#include "../include/time_actions.hpp"


time_actions* time_actions::instance_;
std::mutex time_actions::mutex_;

time_actions* time_actions::get_instance() {
    std::lock_guard<std::mutex> lockguard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new time_actions();
    }
    return instance_;
}

time_actions::time_actions() {
}

time_actions::~time_actions() {
}