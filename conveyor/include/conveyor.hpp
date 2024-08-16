#ifndef CONVEYOR_HPP
#define CONVEYOR_HPP

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <unordered_map>
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"

struct plate {
    private:
        UA_UInt32 position_;
        UA_UInt16 adjacent_robot_;
    public:
        plate(uint32_t _position, uint32_t _adjacent_robot) : position_(_position), adjacent_robot_(_adjacent_robot) {
        }

        ~plate() {
        }

        void set_position(UA_UInt32 _position) {
            position_ = _position;
        }

        void set_adjacent_robot(UA_UInt16 _adjacent_robot) {
            adjacent_robot_ = _adjacent_robot;
        }

        UA_UInt32 get_position() {
            return position_;
        }

        UA_UInt16 get_adjacent_robot() {
            return adjacent_robot_;
        }
};

class conveyor {
private:
    /* conveyor related member variables */
    UA_UInt16 conveyor_port_;
    volatile UA_Boolean running_;
    std::vector<plate> plates_;
    std::unordered_map<UA_UInt32, UA_UInt16> position_to_port_;
    /* clock related member variables */
    UA_Client* clock_client_;
    node_value_subscriber clock_tick_subscriber_;
    UA_UInt64 current_clock_tick_;
    UA_UInt64 next_clock_tick_;
    std::thread client_iterate_thread_;
    method_node_caller receive_tick_ack_caller_;

    static void
    clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                    UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value);
    void
    handle_clock_tick_notification(UA_UInt64 _new_clock_tick);

    static void
    receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_receive_tick_ack_result(UA_Boolean _tick_ack_result);
public:
    conveyor(UA_UInt16 _conveyor_port, UA_UInt16 _robot_start_port, UA_UInt32 _robot_count, UA_UInt16 _clock_port);
    ~conveyor();

    void
    start();

    void
    stop();
};

#endif // CONVEYOR_HPP