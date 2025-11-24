#include "../include/statistics_writer.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <filesystem>

std::mutex statistics_writer::mutex_;
statistics_writer* statistics_writer::instance_;
size_t statistics_writer::robot_count_;

statistics_writer* statistics_writer::get_instance(size_t _robot_count) {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new statistics_writer();
        robot_count_ = _robot_count;
    }
    return instance_;
}

statistics_writer::statistics_writer() {
    boost::interprocess::managed_shared_memory segment(boost::interprocess::create_only, SEGMENT_NAME, SEGMENT_SIZE_BYTES);
    shared_utilization_map_allocator sum_allocator(segment.get_segment_manager());
    composite_utilization_statistics_ = segment.construct<shared_utilization_map>(UTILIZATION_MAP_NAME)(std::less<position_key_t>(), sum_allocator);
    boost::interprocess::named_condition condition(boost::interprocess::create_only, STATISTICS_CONDITION);
    boost::interprocess::named_mutex mutex(boost::interprocess::create_only, STATISTICS_MUTEX);
}

statistics_writer::~statistics_writer() {
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, SEGMENT_NAME);
    segment.destroy<shared_utilization_map>(UTILIZATION_MAP_NAME);
}

void statistics_writer::write_statistics() {
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, SEGMENT_NAME);
    composite_utilization_statistics_ = segment.find<shared_utilization_map>(UTILIZATION_MAP_NAME).first;
    boost::interprocess::named_condition condition(boost::interprocess::open_only, STATISTICS_CONDITION);
    boost::interprocess::named_mutex mutex(boost::interprocess::open_only, STATISTICS_MUTEX);
    boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);
    while(!entries_are_complete()) {
        condition.notify_one();
        condition.wait(lock);
    }
    /* Get statistic results directory */
    char directory_buffer[PATH_MAX + 1];  // +1 for the null terminator
    ssize_t len = readlink("/proc/self/exe", directory_buffer, sizeof(directory_buffer) - 1);
    if (len == -1) {
        perror("readlink");
        return;
    }
    directory_buffer[len] = '\0';  // null terminate
    std::filesystem::path exe_path(directory_buffer);
    std::filesystem::path build_dir = exe_path.parent_path();
    std::filesystem::path statistics_dir = build_dir.parent_path() / "statistic_results";
    /* Find free filename */
    std::ofstream statistics_file;
    int filecount = 0;
    std::stringstream filename;
    filename << "robot-statistics-#" << filecount << ".csv";
    std::filesystem::path statistics_path = statistics_dir / filename.str();
    struct stat filename_buffer;
    for(filecount = 1; (stat(statistics_path.c_str(), &filename_buffer) == 0); filecount++) {
        filename.str("");
        filename << "robot-statistics-#" << filecount << ".csv";
        statistics_path = statistics_dir / filename.str();
    }
    statistics_file.open(statistics_path);
    //Write header
    for(size_t statistics_key = static_cast<size_t>(statistic_key_t::ROBOT_POSITION); statistics_key < static_cast<size_t>(statistic_key_t::METRIC_COUNT); statistics_key++) {
        statistics_file << metric_to_string(statistic_key_t(statistics_key));
        if(statistics_key < static_cast<size_t>(statistic_key_t::METRIC_COUNT)-1) {
            statistics_file << ",";
        } else {
            statistics_file << "\n";
        }
    }
    //Write values (keep metric order like above so that header and values comply)
    for(auto utilization_entry = composite_utilization_statistics_->begin(); utilization_entry != composite_utilization_statistics_->end(); utilization_entry++) {
        auto utilization_map = utilization_entry->second.utilization_map_;
        for(auto utilization_value = utilization_map.begin(); utilization_value != utilization_map.end(); utilization_value++) {
            statistics_file << utilization_entry->first << "," << utilization_value->first << "," << utilization_value->second << "\n";
        }
    }
    statistics_file.close();
}

bool statistics_writer::entries_are_complete() {
    size_t utilization_entry_count = composite_utilization_statistics_->size();
    std::cout << __func__ << " " << utilization_entry_count << std::endl;
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, SEGMENT_NAME);
    std::cout << __func__ << " free memory=" << segment.get_free_memory() << std::endl;
    return utilization_entry_count == robot_count_;
}

void statistics_writer::print_statistics() {
    std::cout << __func__ << std::endl;
    std::stringstream sstream;
    // Write header
    for(size_t statistics_key = static_cast<size_t>(statistic_key_t::ROBOT_POSITION); statistics_key < static_cast<size_t>(statistic_key_t::METRIC_COUNT); statistics_key++) {
        sstream << metric_to_string(statistic_key_t(statistics_key));
        if(statistics_key < static_cast<size_t>(statistic_key_t::METRIC_COUNT)-1) {
            sstream << ",";
        } else {
            sstream << "\n";
        }
    }
    // Write values
    for(auto utilization_entry = composite_utilization_statistics_->begin(); utilization_entry != composite_utilization_statistics_->end(); utilization_entry++) {
        auto utilization_map = utilization_entry->second.utilization_map_;
        for(auto utilization_value = utilization_map.begin(); utilization_value != utilization_map.end(); utilization_value++) {
            sstream << utilization_entry->first << "," << utilization_value->first << "," << utilization_value->second << "\n";
        }
    }
    std::cout << sstream.str() << std::endl;
}