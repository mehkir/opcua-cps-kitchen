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
    UA_Boolean busy_status_;
    UA_UInt32 current_tool_;
    UA_UInt32 current_recipe_id_in_process_;
    volatile UA_Boolean running_;
    method_node_inserter receive_task_inserter_;
    method_node_inserter handover_finished_order_inserter_;
    std::thread robot_server_iterate_thread_;
    /* controller related member variables */
    UA_Client* controller_client_;
    method_node_caller receive_robot_state_caller_;
    std::thread controller_client_iterate_thread_;
    
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
    handle_receive_task(UA_UInt32 _recipe_id, UA_Variant* _output);

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
    join_threads();

public:
    robot(UA_UInt32 _robot_id, UA_UInt16 _robot_port, UA_UInt16 _controller_port);
    ~robot();

    void
    start();

    void
    stop();
};


#endif // ROBOT_HPP