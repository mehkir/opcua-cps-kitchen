#include "../include/timestamp_recorder.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#include <stdexcept>

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
    std::ofstream completed_orders_file;
    int filecount = 0;
    std::stringstream filename;
    filename << "timestamp_results/timepoints-#" << filecount << ".csv";
    struct stat buffer;
    for(filecount = 1; (stat(filename.str().c_str(), &buffer) == 0); filecount++) {
        filename.str("");
        filename << "timestamp_results/timepoints-#" << filecount << ".csv";
    }
    completed_orders_file.open(filename.str());
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