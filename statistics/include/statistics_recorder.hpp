#ifndef STATISTICS_RECORDER_HPP
#define STATISTICS_RECORDER_HPP

#include "shared_memory_parameters.hpp"

class statistics_recorder {
public:
    static statistics_recorder* get_instance();
    void record_timestamp(position_key_t _position, state_key_t _state);
    void contribute_statistics(position_key_t _position);
    ~statistics_recorder();
private:
    static std::mutex mutex_;
    static statistics_recorder* instance_;
    static state_key_t previous_state_;
    std::unordered_map<position_key_t, std::unordered_map<timestamp_key_t, state_key_t>> utilization_statistics_;
    shared_utilization_map* composite_utilization_statistics_;
    statistics_recorder();
};

#endif // STATISTICS_RECORDER_HPP