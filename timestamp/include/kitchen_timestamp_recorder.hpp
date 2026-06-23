/**
 * @file kitchen_timestamp_recorder.hpp
 * @brief OPC UA based timestamp recorder for the kitchen agent.
 * 
 * @details
 * The kitchen timestamp recorder tracks the count of completed orders in the kitchen over time by recording timestamps
 * for each change in the completed orders count. The recorded timestamps are written to a csv file upon destruction of
 * the recorder object. The filename is derived from the kitchen's endpoint.
 */

#ifndef KITCHEN_TIMESTAMP_RECORDER_HPP
#define KITCHEN_TIMESTAMP_RECORDER_HPP

#include <map>
#include <cstdint>
#include <string>
#include <filesystem>

class kitchen_timestamp_recorder {
    private:
        std::map<uint64_t, uint32_t> completed_orders_timestamps_; /**< the map tracking completed orders timestamps. */
        std::map<uint64_t, uint32_t> received_orders_timestamps_; /**< the map tracking received orders timestamps. */
        std::filesystem::path timestamp_dir_; /**< the directory to store timestamp files. */

        void
        write_timestamps(std::string _filename_prefix, std::map<uint64_t, uint32_t> _timestamps_map, std::string _header);
    public:
        /**
         * @brief Constructs a new kitchen timestamp recorder object.
         * 
         */
        kitchen_timestamp_recorder(std::filesystem::path _timestamp_dir) : timestamp_dir_(_timestamp_dir) {
        };

        /**
         * @brief Destroys the kitchen timestamp recorder object.
         * 
         */
        ~kitchen_timestamp_recorder() = default;

        /**
         * @brief Records timestamps for completed orders count changes in the kitchen.
         * 
         * @param _timestamp the timestamp.
         * @param _completed_orders_count the completed orders count.
         */
        void
        record_completed_orders_timestamp(uint64_t _timestamp, uint32_t _completed_orders_count);

        /**
         * @brief Records timestamps for received orders count changes in the kitchen.
         * 
         * @param _timestamp the timestamp.
         * @param _received_orders_count the received orders count.
         */
        void
        record_received_orders_timestamp(uint64_t _timestamp, uint32_t _received_orders_count);

        /**
         * @brief Writes the recorded timestamps to a csv file.
         * 
         */
        void
        write_timestamps();
};

#endif // KITCHEN_TIMESTAMP_RECORDER_HPP