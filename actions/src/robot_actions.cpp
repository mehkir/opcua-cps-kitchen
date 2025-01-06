#include "../include/robot_actions.hpp"

robot_actions* robot_actions::instance_;
std::mutex robot_actions::mutex_;

robot_actions* robot_actions::get_instance() {
    std::lock_guard<std::mutex> lockguard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new robot_actions();
    }
    return instance_;
}

robot_actions::robot_actions() {
}

robot_actions::~robot_actions() {
}