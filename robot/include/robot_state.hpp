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
 * @brief The robot adaptivity states
 * 
 */
enum class robot_adaptivity_state {
    AVAILABLE,
    REARRANGING,
    RECONFIGURING
};

/**
 * @brief Returns the corresponding string for a robot adaptivity state.
 * 
 * @param _adaptivity_state the robot's adaptivity state.
 * @return std::string the corresponding string.
 */
static std::string
robot_adaptivity_state_to_string(robot_adaptivity_state _adaptivity_state) {
    switch (_adaptivity_state) {
        case robot_adaptivity_state::AVAILABLE: return "AVAILABLE";
        case robot_adaptivity_state::REARRANGING: return "REARRANGING";
        case robot_adaptivity_state::RECONFIGURING: return "RECONFIGURING";
        default: return "Unimplemented item";
    }
}

#endif // ROBOT_STATE_HPP