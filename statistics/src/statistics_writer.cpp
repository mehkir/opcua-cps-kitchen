#include "../include/statistics_writer.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>

std::mutex statistics_writer::mutex_;
statistics_writer* statistics_writer::instance_;
size_t statistics_writer::host_count_;
std::string statistics_writer::absolute_results_directory_path_;
std::string statistics_writer::result_filename_;

statistics_writer* statistics_writer::get_instance(size_t _host_count, std::string _absolute_results_directory_path, std::string _result_filename) {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new statistics_writer();
        host_count_ = _host_count;
        absolute_results_directory_path_ = _absolute_results_directory_path;
        result_filename_ = _result_filename;
    }
    return instance_;
}

statistics_writer::statistics_writer() {
    boost::interprocess::managed_shared_memory segment(boost::interprocess::create_only, SEGMENT_NAME, SEGMENT_SIZE_BYTES);
    void_allocator void_allocator_instance(segment.get_segment_manager());
    composite_time_statistics_ = segment.construct<shared_statistics_map>(TIME_STATISTICS_MAP_NAME)(std::less<host_key_t>(), void_allocator_instance);
    boost::interprocess::named_condition condition(boost::interprocess::create_only, STATISTICS_CONDITION);
    boost::interprocess::named_mutex mutex(boost::interprocess::create_only, STATISTICS_MUTEX);
}

statistics_writer::~statistics_writer() {
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, SEGMENT_NAME);
    segment.destroy<shared_statistics_map>(TIME_STATISTICS_MAP_NAME);
}

void statistics_writer::write_statistics() {
    boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, SEGMENT_NAME);
    composite_time_statistics_ = segment.find<shared_statistics_map>(TIME_STATISTICS_MAP_NAME).first;
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
    statistics_file << "HOST,";
    for(metric_key_t metric_key = 0; metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT); metric_key++) {
        statistics_file << time_metric_to_string(time_metric(metric_key));
        if(metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT)-1) {
            statistics_file << ",";
        } else {
            statistics_file << "\n";
        }
    }
    //Write values (keep metric order like above so that header and values comply)
    for(auto host_entry = composite_time_statistics_->begin(); host_entry != composite_time_statistics_->end(); host_entry++) {
        statistics_file << host_entry->first << ",";
        auto metrics_map = host_entry->second.metrics_map_;
        for(metric_key_t metric_key = 0; metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT); metric_key++) {
            if(metrics_map.count(metric_key)) {
                statistics_file << metrics_map[metric_key];
            } else {
                statistics_file << 0;
            }
            if(metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT)-1) {
                statistics_file << ",";
            } else {
                statistics_file << "\n";
            }
        }
    }
    statistics_file.close();
}

bool statistics_writer::entries_are_complete() {
    bool entries_are_complete = false;
    size_t host_entry_count = composite_time_statistics_->size();
    // std::cout << __func__ << " " << host_entry_count << std::endl;
    // boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, SEGMENT_NAME);
    // std::cout << __func__ << " free memory=" << segment.get_free_memory() << std::endl;
    for(auto host_entry = composite_time_statistics_->begin(); (host_entry_count == host_count_) && (host_entry != composite_time_statistics_->end()); host_entry++) {
        auto metrics_map = host_entry->second.metrics_map_;
        entries_are_complete = metrics_map.count(static_cast<metric_key_t>(time_metric::JOB_START)) && metrics_map.count(static_cast<metric_key_t>(time_metric::JOB_END));
        if(!entries_are_complete)
            break;
    }
    return entries_are_complete;
}

void statistics_writer::print_statistics() {
    std::cout << __func__ << std::endl;
    std::stringstream sstream;
    sstream << "HOST,";
    for(metric_key_t metric_key = 0; metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT); metric_key++) {
        sstream << time_metric_to_string(time_metric(metric_key));
        if(metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT)-1) {
            sstream << ",";
        } else {
            sstream << "\n";
        }
    }

    for(auto host_entry = composite_time_statistics_->begin(); host_entry != composite_time_statistics_->end(); host_entry++) {
        sstream << host_entry->first << ",";
        auto metrics_map = host_entry->second.metrics_map_;
        for(metric_key_t metric_key = 0; metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT); metric_key++) {
            if(metrics_map.count(metric_key)) {
                sstream << metrics_map[metric_key];
            } else {
                sstream << 0;
            }
            if(metric_key < static_cast<metric_key_t>(time_metric::TIME_METRIC_COUNT)-1) {
                sstream << ",";
            } else {
                sstream << "\n";
            }
        }
    }

    std::cout << sstream.str() << std::endl;
}