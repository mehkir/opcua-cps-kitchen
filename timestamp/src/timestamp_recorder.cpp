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

void timestamp_recorder::record_timestamp(timepoint_t _timepoint) {
    if(timestamps_.count(_timepoint)) {
        return;
    }
    timestamps_[_timepoint] = std::chrono::system_clock::now().time_since_epoch().count();
}

void timestamp_recorder::write_timestamps() {
    std::ofstream timepoints_file;
    int filecount = 0;
    std::stringstream filename;
    filename << "timestamp_results/timepoints-#" << filecount << ".csv";
    struct stat buffer;
    for(filecount = 1; (stat(filename.str().c_str(), &buffer) == 0); filecount++) {
        filename.str("");
        filename << "timestamp_results/timepoints-#" << filecount << ".csv";
    }
    timepoints_file.open(filename.str());
    //Write header
    for(size_t timepoint_count = 0; timepoint_count < static_cast<size_t>(timepoint_t::TIMEPOINT_COUNT); timepoint_count++) {
        timepoints_file << timepoint_to_string(timepoint_t(timepoint_count));
        if(timepoint_count < static_cast<size_t>(timepoint_t::TIMEPOINT_COUNT)-1) {
            timepoints_file << ",";
        } else {
            timepoints_file << "\n";
        }
    }
    //Write values
    for(size_t timepoint_count = 0; timepoint_count < static_cast<size_t>(timepoint_t::TIMEPOINT_COUNT); timepoint_count++) {
        timepoints_file << timestamps_[timepoint_t(timepoint_count)];
        if(timepoint_count < static_cast<size_t>(timepoint_t::TIMEPOINT_COUNT)-1) {
            timepoints_file << ",";
        } else {
            timepoints_file << "\n";
        }
    }
    timepoints_file.close();
}