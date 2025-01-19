#ifndef SHARED_MEMORY_PARAMETERS_HPP
#define SHARED_MEMORY_PARAMETERS_HPP

#define SEGMENT_NAME                    "statistics_shared_memory"
#define TIME_STATISTICS_MAP_NAME        "time_statistics_shared_map"
#define STATISTICS_MUTEX                "statistics_mutex"
#define STATISTICS_CONDITION            "statistics_condition"
#define SEGMENT_SIZE_BYTES              1048576

#include <mutex>
#include <chrono>
#include <unordered_map>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/named_condition.hpp>
#include <functional>
#include <utility>

//Typedefs of allocators and containers
typedef boost::interprocess::managed_shared_memory::segment_manager                                               segment_manager_t;
typedef boost::interprocess::allocator<void, segment_manager_t>                                                   void_allocator;
typedef std::uint32_t                                                                                             metric_key_t;
typedef std::uint64_t                                                                                             metric_value_t;
typedef std::pair<const metric_key_t, metric_value_t>                                                             metrics_map_value_t;
typedef boost::interprocess::allocator<metrics_map_value_t, segment_manager_t>                                    metrics_map_allocator;
typedef boost::interprocess::map<metric_key_t, metric_value_t, std::less<metric_key_t>, metrics_map_allocator>    metrics_map;

class metrics_map_data {
   public:
      metrics_map metrics_map_;
      metrics_map_data(const void_allocator& void_allocator_instance)
         : metrics_map_(void_allocator_instance)
      {}
};

//Definition of the <host,metrics> map holding an uint32_t as key and metrics_map_data as mapped type
typedef std::uint32_t                                                                                                   host_key_t;
typedef std::pair<const host_key_t, metrics_map_data>                                                                   shared_statistics_map_value_t;
typedef boost::interprocess::allocator<shared_statistics_map_value_t, segment_manager_t>                                shared_statistics_map_allocator;
typedef boost::interprocess::map<host_key_t, metrics_map_data, std::less<host_key_t>, shared_statistics_map_allocator>  shared_statistics_map;

enum class time_metric {
    JOB_START,
    JOB_END,
    TIME_METRIC_COUNT = JOB_END+1
};

static std::string time_metric_to_string(time_metric _time_metric) {
    switch (_time_metric) {
        case time_metric::JOB_START: return "JOB_START";
        case time_metric::JOB_END: return "JOB_END";
        default: std::runtime_error("Unimplemented timepoint");
    }
    return "Unimplemented timepoint";
}

#endif // SHARED_MEMORY_PARAMETERS_HPP