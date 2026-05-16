#include "../include/kitchen_timestamp_recorder.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <stdexcept>
#include <unistd.h>
#include <limits.h>
#include <filesystem>

// Filename prefixe and header for completed orders timestamps
#define COMPLETED_ORDERS_FILENAME_PREFIX "kitchen-completed-orders-#"
#define COMPLETED_ORDERS_HEADER "Timestamp,CompletedOrdersCount\n"
// Filename prefixe and header for received orders timestamps
#define RECEIVED_ORDERS_FILENAME_PREFIX "kitchen-received-orders-#"
#define RECEIVED_ORDERS_HEADER "Timestamp,ReceivedOrdersCount\n"

void
kitchen_timestamp_recorder::record_completed_orders_timestamp(uint64_t _timestamp, uint32_t _completed_orders_count) {
    completed_orders_timestamps_[_timestamp] = _completed_orders_count;
}

void
kitchen_timestamp_recorder::record_received_orders_timestamp(uint64_t _timestamp, uint32_t _received_orders_count) {
    received_orders_timestamps_[_timestamp] = _received_orders_count;
}

void
kitchen_timestamp_recorder::write_timestamps() {
    write_timestamps(COMPLETED_ORDERS_FILENAME_PREFIX, completed_orders_timestamps_, COMPLETED_ORDERS_HEADER);
    write_timestamps(RECEIVED_ORDERS_FILENAME_PREFIX, received_orders_timestamps_, RECEIVED_ORDERS_HEADER);
}

void
kitchen_timestamp_recorder::write_timestamps(std::string _filename_prefix, std::map<uint64_t, uint32_t> _timestamps_map, std::string _header) {
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
    std::ofstream timestamp_file;
    int filecount = 0;
    std::stringstream filename;
    filename << _filename_prefix << filecount << ".csv";
    std::filesystem::path timestamp_path = timestamp_dir / filename.str();
    struct stat filename_buffer;
    for(filecount = 1; (stat(timestamp_path.c_str(), &filename_buffer) == 0); filecount++) {
        filename.str("");
        filename << _filename_prefix << filecount << ".csv";
        timestamp_path = timestamp_dir / filename.str();
    }
    timestamp_file.open(timestamp_path);
    //Write header
    timestamp_file << _header;
    //Write values
    for(auto timestamp_entry : _timestamps_map) {
        timestamp_file << timestamp_entry.first << "," << timestamp_entry.second << "\n";
    }
    timestamp_file.close();
}