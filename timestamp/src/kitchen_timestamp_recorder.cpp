#include "../include/kitchen_timestamp_recorder.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <stdexcept>
#include <unistd.h>
#include <limits.h>
#include <filesystem>

#define FILENAME_PREFIX "kitchen-completed-orders-#"
#define HEADER "Timestamp,CompletedOrdersCount\n"

void kitchen_timestamp_recorder::record_timestamp(uint64_t _timestamp, uint32_t _completed_orders_count) {
    kitchen_timestamps_[_timestamp] = _completed_orders_count;
}

void kitchen_timestamp_recorder::write_timestamps() {
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
    std::ofstream kitchen_completed_orders_file;
    int filecount = 0;
    std::stringstream filename;
    filename << FILENAME_PREFIX << filecount << ".csv";
    std::filesystem::path timestamp_path = timestamp_dir / filename.str();
    struct stat filename_buffer;
    for(filecount = 1; (stat(timestamp_path.c_str(), &filename_buffer) == 0); filecount++) {
        filename.str("");
        filename << FILENAME_PREFIX << filecount << ".csv";
        timestamp_path = timestamp_dir / filename.str();
    }
    kitchen_completed_orders_file.open(timestamp_path);
    //Write header
    kitchen_completed_orders_file << HEADER;
    //Write values
    for(auto kitchen_timestamp_entry : kitchen_timestamps_) {
        kitchen_completed_orders_file << kitchen_timestamp_entry.first << "," << kitchen_timestamp_entry.second << "\n";
    }
    kitchen_completed_orders_file.close();
}