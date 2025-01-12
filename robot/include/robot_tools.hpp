#ifndef ROBOT_TOOLS_HPP
#define ROBOT_TOOLS_HPP

enum class robot_tools {
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

static const char* robot_tools_to_string(robot_tools _robot_tool) {
    switch (_robot_tool) {
    case robot_tools::FRYER : return "FRYER";
    case robot_tools::PAN: return "PAN";
    case robot_tools::POT: return "POT";
    case robot_tools::PEELER: return "PEELER";
    case robot_tools::CUTTER: return "CUTTER";
    case robot_tools::CRUSHER: return "CRUSHER";
    case robot_tools::MASHER: return "MASHER";
    case robot_tools::MIXER: return "MIXER";
    case robot_tools::STIRRER: return "STIRRER";
    case robot_tools::INGREDIENT_DISPENSER: return "INGREDIENT_DISPENSER";
    case robot_tools::LAYERING_DISPENSER: return "LAYERING_DISPENSER";
    case robot_tools::OVEN: return "OVEN";
    case robot_tools::WHISK: return "WHISK";
    default: return "Unimplemented item";
    }
}

#endif