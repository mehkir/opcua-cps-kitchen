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

class kitchen_timestamp_recorder {
    private:
        std::map<uint64_t, uint32_t> kitchen_timestamps_; /**< the map tracking kitchen state timestamps. */
    public:
        /**
         * @brief Constructs a new kitchen timestamp recorder object.
         * 
         */
        kitchen_timestamp_recorder() = default;

        /**
         * @brief Destroys the kitchen timestamp recorder object.
         * 
         */
        ~kitchen_timestamp_recorder() = default;

        /**
         * @brief Records a timestamp for the given kitchen state.
         * 
         * @param _timestamp the timestamp.
         * @param _completed_orders_count the completed orders count.
         */
        void
        record_timestamp(uint64_t _timestamp, uint32_t _completed_orders_count);

        /**
         * @brief Writes the recorded timestamps to a csv file.
         * 
         */
        void
        write_timestamps();
};

#endif // KITCHEN_TIMESTAMP_RECORDER_HPP