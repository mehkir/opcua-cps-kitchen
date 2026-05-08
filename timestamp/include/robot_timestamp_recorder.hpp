#ifndef TIMESTAMP_RECORDER_HPP
#define TIMESTAMP_RECORDER_HPP

#include <map>
#include "types.hpp"
#include "robot_state.hpp"

using namespace cps_kitchen;

typedef std::tuple<position_t, robot_state> timestamp_entry_t;

class robot_timestamp_recorder {
private:
    std::string robot_endpoint_; /**< the robot endpoint for which the timestamps are recorded. */
    std::map<uint64_t, timestamp_entry_t> robot_timestamps_; /**< the map tracking robot state timestamps. */
public:
    /**
     * @brief Constructs a new robot timestamp recorder object.
     * 
     */
    robot_timestamp_recorder(std::string _robot_endpoint);

    /**
     * @brief Destroys the robot timestamp recorder object.
     * 
     */
    ~robot_timestamp_recorder();

    /**
     * @brief Records a timestamp for the given robot position and state.
     * 
     * @param _position the robot position.
     * @param _state the robot state.
     */
    void
    record_timestamp(position_t _position, robot_state _state);

    /**
     * @brief Writes the recorded timestamps to a csv file.
     * 
     */
    void
    write_timestamps();

    /**
     * @brief Sanitizes an endpoint string for use as a filename by replacing invalid characters with underscores.
     * 
     * @param _endpoint the endpoint to sanitize.
     * @return the sanitized endpoint.
     */
    std::string
    sanitize_endpoint(const std::string& _endpoint);
    
};

#endif // TIMESTAMP_RECORDER_HPP