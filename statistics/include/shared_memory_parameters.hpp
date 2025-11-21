#ifndef SHARED_MEMORY_PARAMETERS_HPP
#define SHARED_MEMORY_PARAMETERS_HPP

#define SEGMENT_NAME                    "statistics_shared_memory"
#define UTILIZATION_MAP_NAME            "utilization_shared_map"
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
typedef boost::interprocess::managed_shared_memory::segment_manager                     segment_manager_t;
typedef boost::interprocess::allocator<void, segment_manager_t>                         void_allocator;
typedef std::uint64_t                                                                   timestamp_key_t;
typedef bool                                                                            utilized_value_t;
typedef bool                                                                            retooled_value_t;
typedef std::pair<utilized_value_t, retooled_value_t>                                   utilization_map_value_t;
typedef std::pair<const timestamp_key_t, utilization_map_value_t>                       utilization_map_t;
typedef boost::interprocess::allocator<utilization_map_t, segment_manager_t>            utilization_map_allocator;
typedef boost::interprocess::map<timestamp_key_t, utilization_map_value_t, std::less<timestamp_key_t>, utilization_map_allocator>  utilization_map;

class utilization_map_data {
   public:
      utilization_map utilization_map_;
      utilization_map_data(const void_allocator& void_allocator_instance)
         : utilization_map_(std::less<timestamp_key_t>(), utilization_map_allocator(void_allocator_instance.get_segment_manager()))
      {}
};

//Definition of the <host,metrics> map holding an uint32_t as key and metrics_map_data as mapped type
typedef std::uint32_t                                                                                                   position_key_t;
typedef std::pair<const position_key_t, utilization_map_data>                                                           shared_utilization_map_value_t;
typedef boost::interprocess::allocator<shared_utilization_map_value_t, segment_manager_t>                               shared_utilization_map_allocator;
typedef boost::interprocess::map<position_key_t, utilization_map_data, std::less<position_key_t>, shared_utilization_map_allocator>  shared_utilization_map;

enum class time_metric {
    JOB_START,
    JOB_END,
    TIME_METRIC_COUNT = JOB_END+1
};

inline std::string time_metric_to_string(time_metric _time_metric) {
    switch (_time_metric) {
        case time_metric::JOB_START: return "JOB_START";
        case time_metric::JOB_END: return "JOB_END";
        default: return "Unimplemented timepoint";
    }
}

#endif // SHARED_MEMORY_PARAMETERS_HPP