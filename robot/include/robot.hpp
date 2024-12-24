#ifndef ROBOT_HPP
#define ROBOT_HPP

#include <open62541/server.h>
#include <open62541/client.h>
#include <thread>
#include <queue>
#include <tuple>

#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "types.hpp"

class robot {
private:
    /* robot related member variables */
    UA_Server* server_;
    position_t position_;
    port_t port_;
    UA_Boolean busy_status_;
    UA_UInt32 current_tool_;
    recipe_id_t current_recipe_id_in_process_;
    std::queue<std::tuple<std::string, UA_UInt32>> action_queue_;
    volatile UA_Boolean running_;
    method_node_inserter receive_task_inserter_;
    method_node_inserter handover_finished_order_inserter_;
    std::thread server_iterate_thread_;
    /* controller related member variables */
    UA_Client* controller_client_;
    method_node_caller receive_robot_state_caller_;
    std::thread controller_client_iterate_thread_;
    /* conveyor related member variables */
    UA_Client* conveyor_client_;
    method_node_caller receive_finished_order_notification_caller_;
    std::thread conveyor_client_iterate_thread_;
    
    static void
    receive_robot_state_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_robot_state_result(UA_Boolean _robot_state_received);

    static UA_StatusCode
    receive_task(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);
    
    void
    handle_receive_task(recipe_id_t _recipe_id, UA_Variant* _output);

    static UA_StatusCode
    handover_finished_order(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);

    void
    handle_handover_finished_order(UA_Variant* _output);

    void
    determine_next_action();

    static void
    receive_finished_order_notification_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_finished_order_notification_result(UA_Boolean _finished_order_notification_received);

    static void
    perform_action(UA_Server* _server, void* _data);

    void
    join_threads();

public:
    robot(position_t _position, port_t _port, port_t _controller_port, port_t _conveyor_port);
    ~robot();

    void
    start();

    void
    stop();
};


#endif // ROBOT_HPP