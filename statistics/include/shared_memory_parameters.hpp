#ifndef SHARED_MEMORY_PARAMETERS_HPP
#define SHARED_MEMORY_PARAMETERS_HPP

#define SEGMENT_NAME                    "statistics_shared_memory"
#define UTILIZATION_MAP_NAME            "utilization_shared_map"
#define STATISTICS_MUTEX                "statistics_mutex"
#define STATISTICS_CONDITION            "statistics_condition"
#define SEGMENT_SIZE_BYTES              (1024u * 1024u * 4u)

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
typedef boost::interprocess::managed_shared_memory::segment_manager             segment_manager_t;
typedef std::uint64_t                                                           timestamp_key_t;
typedef std::uint32_t                                                           state_value_t;
typedef std::pair<const timestamp_key_t, state_value_t>                         utilization_map_t;
typedef boost::interprocess::allocator<utilization_map_t, segment_manager_t>    utilization_map_allocator;
typedef boost::interprocess::map<timestamp_key_t, state_value_t, std::less<timestamp_key_t>, utilization_map_allocator>  utilization_map;

class utilization_map_data {
   public:
      utilization_map utilization_map_;
      utilization_map_data(segment_manager_t* _segment_manager)
         : utilization_map_(std::less<timestamp_key_t>(), utilization_map_allocator(_segment_manager))
      {}
};

//Definition of the <host,metrics> map holding an uint32_t as key and metrics_map_data as mapped type
typedef std::uint32_t                                                                                                   position_key_t;
typedef std::pair<const position_key_t, utilization_map_data>                                                           shared_utilization_map_value_t;
typedef boost::interprocess::allocator<shared_utilization_map_value_t, segment_manager_t>                               shared_utilization_map_allocator;
typedef boost::interprocess::map<position_key_t, utilization_map_data, std::less<position_key_t>, shared_utilization_map_allocator>  shared_utilization_map;

enum class statistic_key_t {
    ROBOT_POSITION,
    TIMESTAMP,
    STATE,
    METRIC_COUNT = STATE+1
};

inline std::string metric_to_string(statistic_key_t _metric) {
    switch (_metric) {
        case statistic_key_t::ROBOT_POSITION: return "ROBOT_POSITION";
        case statistic_key_t::TIMESTAMP: return "TIMESTAMP";
        case statistic_key_t::STATE: return "STATE";
        default: return "Unimplemented metric";
    }
}

enum class state_key_t {
    IDLING,
    COOKING,
    RETOOLING,
    WAITING_FOR_PICKUP,
    REARRANGING,
    RECONFIGURING,
    STATE_COUNT = RECONFIGURING+1
};

#endif // SHARED_MEMORY_PARAMETERS_HPP