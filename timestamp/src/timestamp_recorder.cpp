#include "../include/timestamp_recorder.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <limits.h>
#include <filesystem>

std::mutex timestamp_recorder::mutex_;
timestamp_recorder* timestamp_recorder::instance_;

timestamp_recorder* timestamp_recorder::get_instance() {
    std::lock_guard<std::mutex> lockGuard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new timestamp_recorder();
    }
    return instance_;
}

timestamp_recorder::timestamp_recorder() {
}

timestamp_recorder::~timestamp_recorder() {
}

void timestamp_recorder::record_timestamp(uint32_t _completed_order_count) {
    if(timestamps_.count(_completed_order_count)) {
        return;
    }
    timestamps_[_completed_order_count] = std::chrono::system_clock::now().time_since_epoch().count();
}

void timestamp_recorder::write_timestamps() {
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
    std::ofstream completed_orders_file;
    int filecount = 0;
    std::stringstream filename;
    filename << "completed-orders-#" << filecount << ".csv";
    std::filesystem::path timestamp_path = timestamp_dir / filename.str();
    struct stat filename_buffer;
    for(filecount = 1; (stat(timestamp_path.c_str(), &filename_buffer) == 0); filecount++) {
        filename.str("");
        filename << "completed-orders-#" << filecount << ".csv";
        timestamp_path = timestamp_dir / filename.str();
    }
    completed_orders_file.open(timestamp_path);
    //Write header
    for(size_t timestamp_key = 0; timestamp_key < static_cast<size_t>(timestamp_key_t::TIMESTAMP_COUNT); timestamp_key++) {
        completed_orders_file << timestamp_key_to_string(timestamp_key_t(timestamp_key));
        if(timestamp_key < static_cast<size_t>(timestamp_key_t::TIMESTAMP_COUNT)-1) {
            completed_orders_file << ",";
        } else {
            completed_orders_file << "\n";
        }
    }
    //Write values
    for(auto completed_order_entry : timestamps_) {
        completed_orders_file << completed_order_entry.first << "," << completed_order_entry.second << "\n";
    }
    completed_orders_file.close();
}