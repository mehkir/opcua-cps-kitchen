#ifndef TIMESTAMP_RECORDER.HPP
#define TIMESTAMP_RECORDER.HPP

#include <map>
#include <mutex>
#include <chrono>
#include <string>
    
    enum class timepoint_t {
        START,
        END,
        TIMEPOINT_COUNT = END + 1
    };

inline std::string timepoint_to_string(timepoint_t _timepoint) {
    switch (_timepoint) {
        case timepoint_t::START: return "START";
        case timepoint_t::END: return "END";
        default: return "Unimplemented timepoint";
    }
}

    class timestamp_recorder
    {
    private:
        static std::mutex mutex_;
        static timestamp_recorder* instance_;
        std::map<timepoint_t, uint64_t> timestamps_;
        timestamp_recorder();
        ~timestamp_recorder();
    public:
        static timestamp_recorder* get_instance();
        void record_timestamp(timepoint_t _timepoint);
        void write_timestamps();
    };

#endif // TIMESTAMP_RECORDER.HPP