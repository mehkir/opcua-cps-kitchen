#include "../include/kitchen_mape.hpp"
#include <open62541/plugin/log_stdout.h>
#include "controller.hpp"

remote_robot*
kitchen_mape::on_new_order(const std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) {
    simple_reconfiguration(_position_remote_robot_map, _recipe_action_queue);
}

// Simple capability check
remote_robot*
kitchen_mape::simple_capability_check(const std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) {
    remote_robot* suitable_robot = nullptr;
    std::string next_action = _recipe_action_queue.front().get_name();
    for (auto position_remote_robot = _position_remote_robot_map.begin(); position_remote_robot != _position_remote_robot_map.end(); position_remote_robot++) {
        remote_robot* robot = position_remote_robot->second.get();
        if (!robot->is_adaptivity_pending() && robot->is_capable_to(next_action)) {
            suitable_robot = robot;
            break;
        }
    }
    return suitable_robot;
}

// Simple rearranging if suitable robot after next is positioned before next suitable robot
remote_robot*
kitchen_mape::simple_rearranging(const std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) {
    if (_recipe_action_queue.empty()) {
        return nullptr;
    }
    remote_robot* suitable_robot = nullptr;
    std::string next_action = _recipe_action_queue.front().get_name();
    std::queue<robot_action> action_queue_copy = _recipe_action_queue;
    // Determine capable robot
    for (auto position_remote_robot = _position_remote_robot_map.begin(); position_remote_robot != _position_remote_robot_map.end(); position_remote_robot++) {
        remote_robot* robot = position_remote_robot->second.get();
        if (!robot->is_adaptivity_pending() && robot->is_capable_to(next_action)) {
            suitable_robot = robot;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Found next suitable robot at position %d %s", suitable_robot->get_position(), suitable_robot->get_capabilites_string().c_str());
            break;
        }
    }
    
    if (suitable_robot == nullptr) {
        return nullptr;
    }

    // Filter out all actions the suitable robot can do
    do {
        action_queue_copy.pop();
    } while (!action_queue_copy.empty() && suitable_robot->is_capable_to(action_queue_copy.front().get_name()));
    // Determine suitable robot after next
    remote_robot* suitable_robot_after_next = nullptr;
    if (suitable_robot != nullptr && !action_queue_copy.empty()) {
        for (auto position_remote_robot = _position_remote_robot_map.begin(); position_remote_robot != _position_remote_robot_map.end(); position_remote_robot++) {
            remote_robot* robot = position_remote_robot->second.get();
            if (!robot->is_adaptivity_pending() && robot->is_capable_to(action_queue_copy.front().get_name())) {
                suitable_robot_after_next = robot;
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Found next suitable robot after next at position %d %s", suitable_robot_after_next->get_position(), suitable_robot_after_next->get_capabilites_string().c_str());
                break;
            }
        }
    }
    if (suitable_robot != nullptr && suitable_robot_after_next != nullptr
        && (suitable_robot->get_position() > suitable_robot_after_next->get_position())) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Swap robots at position %d and %d", suitable_robot->get_position(), suitable_robot_after_next->get_position());
            swap_robot_positions_callback_(suitable_robot->get_position(), suitable_robot_after_next->get_position());
    }
    return suitable_robot;
}

// Simple reconfiguring by swaping capability profiles if suitable robot after next is positioned before next suitable robot
remote_robot*
kitchen_mape::simple_reconfiguration(const std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) {
    if (_recipe_action_queue.empty()) {
        return nullptr;
    }
    remote_robot* suitable_robot = nullptr;
    std::string next_action = _recipe_action_queue.front().get_name();
    std::string new_possible_profile_for_robot_after_next = "";
    std::string new_possible_profile_for_suitable_robot = "";
    std::queue<robot_action> action_queue_copy = _recipe_action_queue;
    // Determine capable robot
    for (auto position_remote_robot = _position_remote_robot_map.begin(); position_remote_robot != _position_remote_robot_map.end(); position_remote_robot++) {
        remote_robot* robot = position_remote_robot->second.get();
        if (!robot->is_adaptivity_pending() && robot->is_capable_to(next_action)) {
            suitable_robot = robot;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Found next suitable robot at position %d %s", suitable_robot->get_position(), suitable_robot->get_capabilites_string().c_str());
            break;
        }
    }
    
    if (suitable_robot == nullptr) {
        return nullptr;
    }

    for (auto it : get_capabilites()) {
        if (it.second.is_capable_to(next_action)) {
            new_possible_profile_for_robot_after_next = it.first;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Found possible capabilities profile %s for robot after next", it.first.c_str());
            break;
        }
    }

    // Filter out all actions the suitable robot can do
    do {
        action_queue_copy.pop();
    } while (!action_queue_copy.empty() && suitable_robot->is_capable_to(action_queue_copy.front().get_name()));
    // Determine suitable robot after next
    next_action = !action_queue_copy.empty() ? action_queue_copy.front().get_name() : "";
    remote_robot* suitable_robot_after_next = nullptr;
    if (suitable_robot != nullptr && !action_queue_copy.empty()) {
        for (auto position_remote_robot = _position_remote_robot_map.begin(); position_remote_robot != _position_remote_robot_map.end(); position_remote_robot++) {
            remote_robot* robot = position_remote_robot->second.get();
            if (!robot->is_adaptivity_pending() && robot->is_capable_to(next_action)) {
                suitable_robot_after_next = robot;
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Found next suitable robot after next at position %d %s", suitable_robot_after_next->get_position(), suitable_robot_after_next->get_capabilites_string().c_str());
                break;
            }
        }
    }
    
    if (suitable_robot != nullptr && suitable_robot_after_next != nullptr
        && (suitable_robot->get_position() > suitable_robot_after_next->get_position())) {
        for (auto it : get_capabilites()) {
            if (it.second.is_capable_to(next_action)) {
                new_possible_profile_for_suitable_robot = it.first;
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Found possible capabilities profile %s for suitable robot", it.first.c_str());
                break;
            }
        }
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MAPE: Swap capability profiles at position %d and %d with %s and %s, respectively", suitable_robot->get_position(), suitable_robot_after_next->get_position(), new_possible_profile_for_suitable_robot.c_str(), new_possible_profile_for_robot_after_next.c_str());
        reconfigure_robot_callback_(suitable_robot->get_position(), new_possible_profile_for_suitable_robot);
        reconfigure_robot_callback_(suitable_robot_after_next->get_position(), new_possible_profile_for_robot_after_next);
    }
    return suitable_robot;
}