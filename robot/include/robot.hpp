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
#include "robot_state.hpp"
#include "robot_tool.hpp"
#include "recipe_parser.hpp"
#include "session_id.hpp"

using namespace cps_kitchen;

class robot {

private:
    /* robot related member variables */
    UA_Server* server_;
    position_t position_;
    port_t port_;
    robot_state state_;
    robot_tool current_tool_;
    recipe_id_t recipe_id_in_process_;
    session_id session_id_;
    std::string dish_in_process_;
    std::string action_in_process_;
    std::string ingredients_in_process_;
    duration_t overall_time_;
    std::queue<robot_action> action_queue_;
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
    /* recipe related member variables */
    recipe_parser recipe_parser_;
    
    /**
     * @brief Callback called after controller received robot states. Extracts the controller response.
     * 
     * @param _client the client instance from which this method is called
     * @param _userdata the userdata passed to the receive robot state call
     * @param _request_id 
     * @param _response the pointer to the returned parameters
     */
    static void
    receive_robot_state_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /**
     * @brief Handles the controller response from receive_robot_state_called method to indicate if the robot state is not received successfully.
     * 
     * @param _robot_state_received response if robot state is received successfully
     */
    void
    handle_robot_state_result(UA_Boolean _robot_state_received);

    /**
     * @brief Extracts the instruction parameters.
     * 
     * @param _server the server instance from which this method is called
     * @param _session_id 
     * @param _session_context 
     * @param _method_id 
     * @param _method_context the node context data passed to the method node
     * @param _object_id 
     * @param _object_context 
     * @param _input_size the count of the input parameters
     * @param _input the input pointer of the input parameters
     * @param _output_size the allocated output size
     * @param _output the output pointer to store return parameters
     * @return UA_StatusCode 
     */
    static UA_StatusCode
    receive_task(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);
    
    /**
     * @brief Handles the extracted instruction parameters from the receive_task method and processes the ordered dish.
     * 
     * @param _recipe_id the recipe ID of the dish to prepare
     * @param _session_id the session ID between the controller and robot
     * @param _output the output pointer to store return parameters
     */
    void
    handle_receive_task(recipe_id_t _recipe_id, session_id _session_id, UA_Variant* _output);

    /**
     * @brief Initiates the handover of the finished order.
     * 
     * @param _server the server instance from which this method is called
     * @param _session_id 
     * @param _session_context 
     * @param _method_id 
     * @param _method_context the node context data passed to the method node
     * @param _object_id 
     * @param _object_context 
     * @param _input_size the count of the input parameters
     * @param _input the input pointer of the input parameters
     * @param _output_size the allocated output size
     * @param _output the output pointer to store return parameters
     * @return UA_StatusCode 
     */
    static UA_StatusCode
    handover_finished_order(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);

    /**
     * @brief Hands the finished order over to the conveyor.
     * 
     * @param _output the output pointer to store return parameters
     */
    void
    handle_handover_finished_order(UA_Variant* _output);

    /**
     * @brief Determines if there are still open steps or retooling is necessary for the current dish in process.
     * If the dish is done, the conveyor is notified about it.
     * 
     */
    void
    determine_next_action();

    /**
     * @brief Callback called after conveyor is notified about finished dish. Extracts the conveyor response.
     * 
     * @param _client the client instance from which this method is called
     * @param _userdata the userdata passed to the receive robot state call
     * @param _request_id 
     * @param _response the pointer to the returned parameters
     */
    static void
    receive_finished_order_notification_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /**
     * @brief Handles the conveyor response from receive_finished_order_notification_called method to indicate if the notification is not received successfully.
     * 
     * @param _finished_order_notification_received 
     */
    void
    handle_finished_order_notification_result(UA_Boolean _finished_order_notification_received);

    /**
     * @brief Timed callback to indicate the current action completion and to update the overall time for the current dish.
     * 
     * @param _server the server instance from which this method is called
     * @param _data the data passed to the scheduling call
     */
    static void
    perform_action(UA_Server* _server, void* _data);

    /**
     * @brief Timed callback to indicate retooling completion and to update the current tool and overall time.
     * 
     * @param _server the server instance from which this method is called
     * @param _data the data passed to the scheduling call
     */
    static void
    retool(UA_Server* _server, void* _data);

    /**
     * @brief Joins all started threads.
     * 
     */
    void
    join_threads();

public:
    /**
     * @brief Construct a new robot object.
     * 
     * @param _position the position of the robot at the conveyor
     * @param _port the port of the robot
     * @param _controller_port the port of the controller
     * @param _conveyor_port the port of the conveyor
     */
    robot(position_t _position, port_t _port, port_t _controller_port, port_t _conveyor_port);

    /**
     * @brief Destroy the robot object.
     * 
     */
    ~robot();

    /**
     * @brief Checks if initialization was successful, sends the initial robot states and joins all started threads.
     * 
     */
    void
    start();

    /**
     * @brief Stops the robot and shuts it down.
     * 
     */
    void
    stop();
};


#endif // ROBOT_HPP