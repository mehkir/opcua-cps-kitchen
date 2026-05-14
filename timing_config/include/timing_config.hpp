#ifndef TIMING_CONFIG_HPP
#define TIMING_CONFIG_HPP

#include <mutex>
#include <string>
#include <unordered_map>
#include "types.hpp"
/* Agent time key names */
#define ROBOT_TIMES "robot_times"
#define CONVEYOR_TIMES "conveyor_times"
/* Robot action time names */
#define ROBOT_MOVE "move"
#define RECONFIGURATION "reconfiguration"
#define RETOOL "retool"
#define ACTION_FACTOR "action_factor"
/* Conveyor action time names */
#define DEBOUNCE "debounce"
#define CONVEYOR_MOVE "move"

using namespace cps_kitchen;

class timing_config {
    private:
        /**
         * @brief Constructs a new timing config object.
         * 
         */
        timing_config();

        /**
         * @brief Destroys the timing config object.
         * 
         */
        ~timing_config();

        /**
         * @brief The singleton timing_config instance pointer.
         * 
         */
        static timing_config* instance_;

        /**
         * @brief The mutex ensuring the singleton instance.
         * 
         */
        static std::mutex mutex_;

        /**
         * @brief The timing configuration data map.
         * 
         */
        std::unordered_map<std::string, std::unordered_map<std::string, duration_t>> timing_map_;

        /**
         * @brief Loads the timing configuration data from the source file.
         * 
         */
        void
        load_timing_config();
    public:
        /**
         * @brief Returns the singleton timing_config instance.
         * 
         * @return timing_config* the singleton timing_config instance pointer.
         */
        static timing_config* get_instance();

        /**
         * @brief Returns the timing value for the given agent and timing name.
         * 
         * @param _agent_name the agent name.
         * @param _timing_name the timing name.
         * @return duration_t the timing value.
         */
        duration_t
        get_timing(const std::string _agent_name, const std::string _timing_name) const;

    };




#endif // TIMING_CONFIG_HPP