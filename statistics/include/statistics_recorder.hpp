#ifndef STATISTICS_RECORDER_HPP
#define STATISTICS_RECORDER_HPP

#include "shared_memory_parameters.hpp"

class statistics_recorder
{
public:
    static statistics_recorder* get_instance();
    void record_timestamp(position_key_t _position, utilized_value_t _utilized, retooled_value_t _retooled);
    void contribute_statistics();
    ~statistics_recorder();
private:
    static std::mutex mutex_;
    static statistics_recorder* instance_;
    std::unordered_map<position_key_t, std::unordered_map<timestamp_key_t, std::pair<utilized_value_t, retooled_value_t>>> utilization_statistics_;
    shared_utilization_map* composite_utilization_statistics_;
    statistics_recorder();
};

#endif // STATISTICS_RECORDER_HPP