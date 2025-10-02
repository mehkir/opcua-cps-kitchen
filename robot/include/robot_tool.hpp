/**
 * @file robot_tool.hpp
 * @brief Defines the available robot tools
 *
 * This header defines the available cooking tools of the kitchen robot agent
 */
#ifndef ROBOT_TOOL_HPP
#define ROBOT_TOOL_HPP

/**
 * @brief The available robot tools
 * 
 */
enum class robot_tool {
    FRYER,
    PAN,
    POT,
    PEELER,
    CUTTER,
    CRUSHER,
    MASHER,
    MIXER,
    STIRRER,
    INGREDIENT_DISPENSER,
    LAYERING_DISPENSER,
    OVEN,
    WHISK,
    ROBOT_TOOLS_COUNT = WHISK+1
};
/**
 * @brief Returns the corresponding string for the given robot tool
 * 
 * @param _robot_tool the robot tool
 * @return const char* the corresponding string
 */
static const char* robot_tool_to_string(robot_tool _robot_tool) {
    switch (_robot_tool) {
    case robot_tool::FRYER : return "FRYER";
    case robot_tool::PAN: return "PAN";
    case robot_tool::POT: return "POT";
    case robot_tool::PEELER: return "PEELER";
    case robot_tool::CUTTER: return "CUTTER";
    case robot_tool::CRUSHER: return "CRUSHER";
    case robot_tool::MASHER: return "MASHER";
    case robot_tool::MIXER: return "MIXER";
    case robot_tool::STIRRER: return "STIRRER";
    case robot_tool::INGREDIENT_DISPENSER: return "INGREDIENT_DISPENSER";
    case robot_tool::LAYERING_DISPENSER: return "LAYERING_DISPENSER";
    case robot_tool::OVEN: return "OVEN";
    case robot_tool::WHISK: return "WHISK";
    default: return "Unimplemented item";
    }
}

#endif // ROBOT_TOOL_HPP