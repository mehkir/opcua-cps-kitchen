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

/**
 * @brief The robot states
 * 
 */
enum class robot_state {
    IDLING,
    COOKING,
    RETOOLING,
    WAITING_FOR_PICKUP,
    REARRANGING,
    RECONFIGURING
};

/**
 * @brief Returns the corresponding string for a robot state.
 * 
 * @param _state the robot's state.
 * @return std::string the corresponding string.
 */
static std::string
robot_state_to_string(robot_state _state) {
    switch (_state) {
        case robot_state::IDLING: return "IDLING";
        case robot_state::COOKING: return "COOKING";
        case robot_state::RETOOLING: return "RETOOLING";
        case robot_state::WAITING_FOR_PICKUP: return "WAITING_FOR_PICKUP";
        case robot_state::REARRANGING: return "REARRANGING";
        case robot_state::RECONFIGURING: return "RECONFIGURING";
        default: return "Unimplemented item";
    }
}

#endif // ROBOT_STATE_HPP