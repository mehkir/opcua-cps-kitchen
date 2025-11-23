#include "../include/statistics_writer.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>

std::mutex statistics_writer::mutex_;
statistics_writer* statistics_writer::instance_;
size_t statistics_writer::robot_count_;
std::string statistics_writer::absolute_results_directory_path_;
std::string statistics_writer::result_filename_;

statistics_writer* statistics_writer::get_instance(size_t _host_count, std::string _absolute_results_directory_path, std::string _result_filename) {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new statistics_writer();
        robot_count_ = _host_count;
        absolute_results_directory_path_ = _absolute_results_directory_path;
        result_filename_ = _result_filename;
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
    std::ofstream statistics_file;
    int filecount = 0;
    std::stringstream absolute_result_file_path;
    absolute_result_file_path << absolute_results_directory_path_ << result_filename_ << "-#" << filecount << ".csv";
    struct stat buffer;
    //Choose unused/non-existing absolute_result_file_path
    for(filecount = 1; (stat(absolute_result_file_path.str().c_str(), &buffer) == 0); filecount++) {
        absolute_result_file_path.str("");
        absolute_result_file_path << absolute_results_directory_path_ << result_filename_ << "-#" << filecount << ".csv";
    }
    statistics_file.open(absolute_result_file_path.str());
    //Write header
    for(size_t metric_idx = static_cast<size_t>(statistic_key_t::ROBOT_POSITION); metric_idx < static_cast<size_t>(statistic_key_t::METRIC_COUNT); metric_idx++) {
        statistics_file << metric_to_string(statistic_key_t(metric_idx));
        if(metric_idx < static_cast<size_t>(statistic_key_t::METRIC_COUNT)-1) {
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
    // std::cout << __func__ << " " << utilization_entry_count << std::endl;
    // boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, SEGMENT_NAME);
    // std::cout << __func__ << " free memory=" << segment.get_free_memory() << std::endl;
    return utilization_entry_count == robot_count_;
}

void statistics_writer::print_statistics() {
    std::cout << __func__ << std::endl;
    std::stringstream sstream;
    // Write header
    for(size_t metric_idx = static_cast<size_t>(statistic_key_t::ROBOT_POSITION); metric_idx < static_cast<size_t>(statistic_key_t::METRIC_COUNT); metric_idx++) {
        sstream << metric_to_string(statistic_key_t(metric_idx));
        if(metric_idx < static_cast<size_t>(statistic_key_t::METRIC_COUNT)-1) {
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