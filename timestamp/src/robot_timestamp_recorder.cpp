#include "../include/robot_timestamp_recorder.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <stdexcept>
#include <unistd.h>
#include <limits.h>
#include <filesystem>

#define HEADER "Timestamp,Position,State\n"
#define SCHEME_DELIMITER "://"

robot_timestamp_recorder::robot_timestamp_recorder(std::string _robot_endpoint) : robot_endpoint_(_robot_endpoint) {
}

robot_timestamp_recorder::~robot_timestamp_recorder() {
}

void robot_timestamp_recorder::record_timestamp(position_t _position, robot_state _state) {
    uint64_t timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    timestamp_entry_t entry = std::make_tuple(_position, _state);
    robot_timestamps_[timestamp] = entry;
}

void robot_timestamp_recorder::write_timestamps() {
    /* Get timestamp results directory */
    char directory_buffer[PATH_MAX + 1];  // +1 for the null terminator
    ssize_t len = readlink("/proc/self/exe", directory_buffer, sizeof(directory_buffer) - 1);
    if (len == -1) {
        perror("readlink");
        return;
    }
    directory_buffer[len] = '\0';  // null terminate
    std::filesystem::path exe_path(directory_buffer);
    std::filesystem::path build_dir = exe_path.parent_path();
    std::filesystem::path timestamp_dir = build_dir.parent_path() / "timestamp_results";
    /* Find free filename */
    std::ofstream robot_states_file;
    int filecount = 0;
    std::stringstream filename;
    std::string sanitized_endpoint = sanitize_endpoint(robot_endpoint_);
    filename << sanitized_endpoint << "-robot-states-#" << filecount << ".csv";
    std::filesystem::path timestamp_path = timestamp_dir / filename.str();
    struct stat filename_buffer;
    for(filecount = 1; (stat(timestamp_path.c_str(), &filename_buffer) == 0); filecount++) {
        filename.str("");
        filename << sanitized_endpoint << "-robot-states-#" << filecount << ".csv";
        timestamp_path = timestamp_dir / filename.str();
    }
    robot_states_file.open(timestamp_path);
    //Write header
    robot_states_file << HEADER;
    //Write values
    for(auto robot_state_entry : robot_timestamps_) {
        robot_states_file << robot_state_entry.first << "," << std::get<0>(robot_state_entry.second) << "," << static_cast<int>(std::get<1>(robot_state_entry.second)) << "\n";
    }
    robot_states_file.close();
}

std::string robot_timestamp_recorder::sanitize_endpoint(const std::string& _endpoint) {
    std::string sanitized = _endpoint;
    auto pos = sanitized.find(SCHEME_DELIMITER);
    if (pos != std::string::npos)
        sanitized = sanitized.substr(pos + strlen(SCHEME_DELIMITER));
    
    for (char& c : sanitized) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    return sanitized;
}