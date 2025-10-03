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
    IDLING,
    COOKING,
    FINISHED
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
        case robot_state::IDLING: return "IDLING";
        case robot_state::COOKING: return "COOKING";
        case robot_state::FINISHED: return "FINISHED";
        default: return "Unimplemented item";
    }
}

#endif // ROBOT_STATE_HPP