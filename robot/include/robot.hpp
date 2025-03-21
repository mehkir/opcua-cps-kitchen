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
#include "robot_tool.hpp"
#include "recipe_parser.hpp"
#include "capability_parser.hpp"

using namespace cps_kitchen;

class robot {

private:
    /* robot related member variables */
    UA_Server* server_;
    position_t position_;
    port_t port_;
    robot_tool current_tool_;
    recipe_id_t recipe_id_in_process_;
    UA_UInt32 processed_steps_of_recipe_id_in_process_;
    port_t next_suitable_robot_port_for_recipe_id_in_process_;
    position_t next_suitable_robot_position_for_recipe_id_in_process_;
    std::string dish_in_process_;
    std::string action_in_process_;
    std::string ingredients_in_process_;
    duration_t overall_time_;
    std::queue<robot_action> action_queue_;
    volatile UA_Boolean running_;
    method_node_inserter receive_task_inserter_;
    method_node_inserter handover_finished_order_inserter_;
    std::thread server_iterate_thread_;
    recipe_parser recipe_parser_;
    capability_parser capability_parser_;
    UA_String* capabilities_;
    /* controller related member variables */
    UA_Client* controller_client_;
    method_node_caller register_robot_caller_;
    method_node_caller choose_next_robot_caller_;
    std::thread controller_client_iterate_thread_;
    /* conveyor related member variables */
    UA_Client* conveyor_client_;
    method_node_caller receive_finished_order_notification_caller_;
    std::thread conveyor_client_iterate_thread_;

    /**
     * @brief Callback called after controller received robot registration. Extracts the controller response.
     * 
     * @param _client the client instance from which this method is called
     * @param _userdata the userdata passed to the receive robot state call
     * @param _request_id 
     * @param _response the pointer to the returned parameters
     */
    static void
    register_robot_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /**
     * @brief Handles the controller response from register_robot_called method to indicate if the robot registration is received successfully.
     * The robot sends its state to the controller on success.
     * 
     * @param _register_robot_received response if robot registration is received successfully
     */
    void
    handle_register_robot_result(UA_Boolean _register_robot_received);

    /**
     * @brief Callback called after controller received choose next robot request. Extracts the controller response.
     * 
     * @param _client the client instance from which this method is called
     * @param _userdata the userdata passed to the receive robot state call
     * @param _request_id 
     * @param _response the pointer to the returned parameters
     */
    static void
    choose_next_robot_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /**
     * @brief Handles the controller response from choose_next_robot_called method to tell the conveyor where to deliver the plate next during the handover.
     * 
     * @param _target_port next suitable robot's port
     * @param _target_position next suitable robot's position
     */
    void
    handle_choose_next_robot_result(port_t _target_port, position_t _target_position);

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
     * @param _processed_steps the processed steps of the recipe ID so far
     * @param _output the output pointer to store return parameters
     */
    void
    handle_receive_task(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_Variant* _output);

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
     * @param _data the robot instance passed to the scheduling call
     */
    static void
    retool(UA_Server* _server, void* _data);

    /**
     * @brief Updates an information node
     * 
     * @param _server the server 
     * @param _ns_index the namespace index
     * @param _node_name the node name
     * @param _value the value
     * @param _type_index the type index of the data type
     */
    void
    update_information_node(UA_Server* _server, UA_UInt16 _ns_index, std::string _node_name, void* _value, UA_UInt32 _type_index);

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