#ifndef CONVEYOR_HPP
#define CONVEYOR_HPP

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"

struct plate {
    private:
        UA_UInt32 id_;
        UA_UInt32 robot_id_;
    public:
        plate(uint32_t _id, uint32_t _robot_id) : id_(_id), robot_id_(_robot_id) {

        }

        ~plate() {

        }
};

class conveyor {
private:
    UA_UInt16 conveyor_port_;
    volatile UA_Boolean running_;
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
    conveyor(UA_UInt16 _conveyor_port, UA_UInt32 _robot_count, UA_UInt16 _clock_port);
    ~conveyor();

    void
    start();

    void
    stop();
};

#endif // CONVEYOR_HPP