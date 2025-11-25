#ifndef TIMESTAMP_RECORDER_HPP
#define TIMESTAMP_RECORDER_HPP

#include <map>
#include <mutex>
#include <chrono>
#include <string>
    
    enum class timestamp_key_t {
        COMPLETED_ORDER_COUNT,
        TIMESTAMP,
        TIMESTAMP_COUNT = TIMESTAMP + 1
    };

inline std::string timestamp_key_to_string(timestamp_key_t _timepoint) {
    switch (_timepoint) {
        case timestamp_key_t::COMPLETED_ORDER_COUNT: return "COMPLETED_ORDER_COUNT";
        case timestamp_key_t::TIMESTAMP: return "TIMESTAMP";
        default: return "Unimplemented timepoint";
    }
}

    class timestamp_recorder
    {
    private:
        static std::mutex mutex_;
        static timestamp_recorder* instance_;
        std::map<uint32_t, uint64_t> timestamps_;
        timestamp_recorder();
        ~timestamp_recorder();
    public:
        static timestamp_recorder* get_instance();
        void record_timestamp(uint32_t _completed_order_count);
        void write_timestamps();
    };

#endif // TIMESTAMP_RECORDER.HPP