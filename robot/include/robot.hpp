#ifndef ROBOT_HPP
#define ROBOT_HPP

#include <open62541/server.h>
#include <open62541/client.h>
#include <thread>

#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"

class robot {
private:
    /* robot related member variables */
    UA_Server* robot_server_;
    UA_UInt32 robot_id_;
    UA_UInt16 robot_port_;
    volatile UA_Boolean running_;
    UA_Boolean busy_status_;
    method_node_inserter receive_task_inserter_;
    /* clock related member variables */
    UA_Client* clock_client_;
    node_value_subscriber clock_tick_subscriber_;
    UA_Int64 current_clock_tick_;
    UA_Int64 next_clock_tick_;
    std::thread clock_client_iterate_thread_;
    method_node_caller receive_tick_ack_caller_;
    /* controller related member variables */
    UA_Client* controller_client_;
    method_node_caller receive_robot_state_caller_;
    std::thread controller_client_iterate_thread_;

    static void
    clock_tick_notification_callback(UA_Client* client, UA_UInt32 subscription_id, void* subscription_context,
                                    UA_UInt32 monitor_id, void* monitor_context, UA_DataValue* value);
    void
    handle_clock_tick_notification(UA_UInt64 _new_clock_tick);

    static void
    receive_tick_ack_called(UA_Client* client, void* userdata, UA_UInt32 request_id, UA_CallResponse* response);

    void
    handle_receive_tick_ack_result(UA_Boolean _tick_ack_result);

    static void
    receive_robot_state_called(UA_Client* client, void* userdata, UA_UInt32 request_id, UA_CallResponse* response);

    void
    handle_receive_robot_state_result(UA_Boolean _robot_state_received);

    static UA_StatusCode
    receive_task(UA_Server *server,
            const UA_NodeId *session_id, void *session_context,
            const UA_NodeId *method_id, void *method_context,
            const UA_NodeId *object_id, void *object_context,
            size_t input_size, const UA_Variant *input,
            size_t output_size, UA_Variant *output);
    
    void
    handle_receive_task(UA_UInt32 _activity_id, UA_UInt32 _ingredient_id, UA_Variant* _output);

public:
    robot(UA_UInt32 _robot_id, UA_UInt16 _robot_port, UA_UInt16 _clock_port, UA_UInt16 _controller_port);
    ~robot();

    void
    start();

    void
    stop();
};


#endif // ROBOT_HPP