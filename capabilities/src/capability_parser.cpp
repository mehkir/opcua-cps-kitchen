#include "../include/capability_parser.hpp"
#include "robot_actions.hpp"

#include <jsoncpp/json/json.h>
#include <fstream>
#include <unistd.h>
#include <limits.h>
#include <filesystem>
#include <iostream>

capability_parser::capability_parser(std::string _capabilities_file_name) {
    robot_actions* actions = robot_actions::get_instance();
    char buffer[PATH_MAX + 1];  // +1 for the null terminator
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len == -1) {
        perror("readlink");
        return;
    }
    buffer[len] = '\0';  // null terminate
    std::filesystem::path exe_path(buffer);
    std::filesystem::path exe_dir = exe_path.parent_path();
    std::filesystem::path capabilities_file_path = exe_dir.parent_path() / "capabilities" / _capabilities_file_name;
    std::ifstream ifs_capabilities(capabilities_file_path.string());
    Json::Value capabilities;
    Json::Reader reader;
    if (!reader.parse(ifs_capabilities, capabilities)) {
        std::cerr << reader.getFormattedErrorMessages() << std::endl;
    }
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

std::unordered_set<std::string> capability_parser::get_capabilities() {
    return capabilities_;
}