#include "../include/statistics_recorder.hpp"

#include <iostream>
#include <stdexcept>

std::mutex statistics_recorder::mutex_;
statistics_recorder* statistics_recorder::instance_;

statistics_recorder* statistics_recorder::get_instance() {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new statistics_recorder();
    }
    return instance_;
}

statistics_recorder::statistics_recorder() {
}

statistics_recorder::~statistics_recorder() {
}

void statistics_recorder::record_timestamp(position_key_t _position, state_key_t _state) {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    utilization_statistics_[_position][std::chrono::system_clock::now().time_since_epoch().count()] = _state;
}

void statistics_recorder::contribute_statistics(position_key_t _position) {
    std::cout << "[<statistics_recorder>] (" << __func__ << ") robot at position" << _position << "is contributing ..." << std::endl;
    bool waited_for_shm = false;
    for (bool shared_objects_initialized = false; !shared_objects_initialized;) {
        try {
            boost::interprocess::named_condition condition(boost::interprocess::open_only, STATISTICS_CONDITION);
            boost::interprocess::named_mutex mutex(boost::interprocess::open_only, STATISTICS_MUTEX);
            boost::interprocess::managed_shared_memory msm(boost::interprocess::open_only, SEGMENT_NAME);
            {
                boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);
                while (!(composite_utilization_statistics_ = msm.find<shared_utilization_map>(UTILIZATION_MAP_NAME).first)) {
                    waited_for_shm = true;
                    condition.wait(lock);
                    std::cout << "[<statistics_recorder>] (" << __func__ << ") shared map not intialized yet" << std::endl;
                }
            }
            if(waited_for_shm) {
                std::cout << "[<statistics_recorder>] (" << __func__ << ") resume composing" << std::endl;
            }
            
            {
                boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);
                for(auto utilization_entry : utilization_statistics_) {
                    utilization_map_data* map_data;
                    if(composite_utilization_statistics_->count(utilization_entry.first)) {
                        map_data = &composite_utilization_statistics_->at(utilization_entry.first);
                    } else {
                        utilization_map_data utilization_map_data_var = utilization_map_data(msm.get_segment_manager());
                        map_data = &utilization_map_data_var;
                    }
                    for(auto value_entry : utilization_entry.second) {
                        map_data->utilization_map_[value_entry.first] = (u_int32_t)value_entry.second;
                    }
                    composite_utilization_statistics_->insert({utilization_entry.first, *map_data});
                }
                shared_objects_initialized = true;
            }
            std::cout << "[<statistics_recorder>] (" << __func__ << ") robot at position" << _position << "contributed successfully" << std::endl;
            condition.notify_one();
        } catch (boost::interprocess::interprocess_exception& interprocess_exception) {
            std::cerr << __func__ << interprocess_exception.what() << std::endl;
            std::cout << "[<statistics_recorder>] (" << __func__ << ") shared objects may not created yet or segment size is not enough. Examine error message for exact cause." << std::endl;
            sleep(1);
        }
    }
}