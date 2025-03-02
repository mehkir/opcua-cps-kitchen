#include "../include/capability_parser.hpp"
#include "robot_actions.hpp"

#include <jsoncpp/json/json.h>
#include <fstream>

capability_parser::capability_parser(std::string _capabilities_path, position_t _robot_position) {
    robot_actions* actions = robot_actions::get_instance();
    std::string capabilities_file_path = _capabilities_path + "r" + std::to_string(_robot_position) + ".json";
    std::ifstream ifs_capabilities(capabilities_file_path);
    Json::Value capabilities;
    Json::Reader reader;
    reader.parse(ifs_capabilities, capabilities);
    for (auto capability : capabilities["capabilities"]) {
        if (!actions->has_action(capability.asString())) {
            std::string error_string = capability.asString() + " is not a valid action";
            throw std::invalid_argument(error_string);
        }
        capabilities_.insert(capability.asString());
    }
}

capability_parser::~capability_parser() {
}

bool capability_parser::is_capable_to(std::string _action_name) {
    return capabilities_.find(_action_name) != capabilities_.end();
}