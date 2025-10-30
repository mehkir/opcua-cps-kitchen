/**
 * @file robot_state.hpp
 * @brief Defines the robot states.
 *
 * This header defines the robot states of the kitchen robot agent.
 */
#ifndef ROBOT_STATE_HPP
#define ROBOT_STATE_HPP

#include <string>

/**
 * @brief The robot states
 * 
 */
enum class robot_state {
    AVAILABLE,
    REARRANGING,
    RECONFIGURING
};

/**
 * @brief Returns the corresponding string for a robot state.
 * 
 * @param _state the robot state.
 * @return std::string the corresponding string.
 */
static std::string
robot_state_to_string(robot_state _state) {
    switch (_state) {
        case robot_state::AVAILABLE: return "AVAILABLE";
        case robot_state::REARRANGING: return "REARRANGING";
        case robot_state::RECONFIGURING: return "RECONFIGURING";
        default: return "Unimplemented item";
    }
}

#endif // ROBOT_STATE_HPP