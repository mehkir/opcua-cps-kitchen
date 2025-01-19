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

void timestamp_recorder::record_timestamp(timepoint _timepoint) {
    if(timestamps_.count(_timepoint)) {
        std::string error_string = "There is already a timestamp for the key: " + timepoint_to_string(_timepoint);
        throw std::runtime_error(error_string);
    }
    timestamps_[_timepoint] = std::chrono::system_clock::now();
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
    for(size_t timepoint_count = 0; timepoint_count < static_cast<size_t>(timepoint::TIMEPOINT_COUNT); timepoint_count++) {
        timepoints_file << timepoint_to_string(timepoint(timepoint_count));
        if(timepoint_count < static_cast<size_t>(timepoint::TIMEPOINT_COUNT)-1) {
            timepoints_file << ",";
        } else {
            timepoints_file << "\n";
        }
    }
    //Write values
    for(size_t timepoint_count = 0; timepoint_count < static_cast<size_t>(timepoint::TIMEPOINT_COUNT); timepoint_count++) {
        timepoints_file << timestamps_[timepoint(timepoint_count)].time_since_epoch().count();
        if(timepoint_count < static_cast<size_t>(timepoint::TIMEPOINT_COUNT)-1) {
            timepoints_file << ",";
        } else {
            timepoints_file << "\n";
        }
    }
    timepoints_file.close();
}

std::string timestamp_recorder::timepoint_to_string(timepoint _timepoint) {
    switch (_timepoint) {
    case timepoint::JOB_START: return "JOB_START";
    case timepoint::JOB_END: return "JOB_END";
    default: std::runtime_error("Unimplemented timepoint");
    }
}