#ifndef TIMESTAMP_RECORDER.HPP
#define TIMESTAMP_RECORDER.HPP

#include <map>
#include <mutex>
#include <chrono>
    
    enum class timepoint {
        JOB_START,
        JOB_END,
        TIMEPOINT_COUNT = JOB_END + 1
    };

    class timestamp_recorder
    {
    private:
        static std::mutex mutex_;
        static timestamp_recorder* instance_;
        std::map<timepoint,std::chrono::time_point<std::chrono::system_clock>> timestamps_;
        timestamp_recorder();
        ~timestamp_recorder();
        std::string timepoint_to_string(timepoint _timepoint);
    public:
        static timestamp_recorder* get_instance();
        void record_timestamp(timepoint _timepoint);
        void write_timestamps();
    };

#endif // TIMESTAMP_RECORDER.HPP