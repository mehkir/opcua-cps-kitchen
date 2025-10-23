#include "../include/kitchen_mape.hpp"
#include "controller.hpp"

remote_robot*
kitchen_mape::on_new_order(const std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) {
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

void
kitchen_mape::set_swap_robot_positions_callback(swap_robot_positions_callback_t _swap_robot_positions_callback) {
    swap_robot_positions_callback_ = _swap_robot_positions_callback;
}

void
kitchen_mape::set_reconfigure_robot_callback(reconfigure_robot_callback_t _reconfigure_robot_callback) {
    reconfigure_robot_callback_ = _reconfigure_robot_callback;
}