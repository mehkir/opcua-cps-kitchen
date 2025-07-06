#ifndef ROBOT_HPP
#define ROBOT_HPP

#include <open62541/server.h>
#include <open62541/client.h>
#include <thread>
#include <queue>

#include "method_node_caller.hpp"
#include "types.hpp"
#include "robot_tool.hpp"
#include "recipe_parser.hpp"
#include "capability_parser.hpp"
#include "object_type_node_inserter.hpp"
#include "node_browser_helper.hpp"

using namespace cps_kitchen;

struct order {
    private:
        recipe_id_t recipe_id_;
        UA_UInt32 processed_steps_;
        std::queue<robot_action> action_queue_;
    public:
        order(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, std::queue<robot_action> _action_queue) : recipe_id_(_recipe_id), processed_steps_(_processed_steps), action_queue_(_action_queue) {
        }

        recipe_id_t get_recipe_id() const {
            return recipe_id_;
        }

        UA_UInt32 get_processed_steps() const {
            return processed_steps_;
        }

        std::queue<robot_action> get_action_queue() const {
            return action_queue_;
        }
};

class robot {

private:
    /* robot related member variables */
    UA_Server* server_;
    position_t position_;
    port_t port_;
    object_type_node_inserter robot_type_inserter_;
    robot_tool current_tool_;
    UA_UInt32 processed_steps_of_recipe_id_in_process_;
    port_t next_suitable_robot_port_for_recipe_id_in_process_;
    position_t next_suitable_robot_position_for_recipe_id_in_process_;
    std::queue<order> order_queue_;
    duration_t current_action_duration_;
    std::queue<robot_action> action_queue_in_process_;
    volatile UA_Boolean running_;
    std::thread server_iterate_thread_;
    recipe_parser recipe_parser_;
    capability_parser capability_parser_;
    std::unordered_map<std::string, object_method_info> method_id_map_;
    /* controller related member variables */
    UA_Client* controller_client_;
    std::thread controller_client_iterate_thread_;
    /* conveyor related member variables */
    UA_Client* conveyor_client_;
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
     * @brief Cooks the next order in the order queue
     * 
     */
    void
    cook_next_order();

    /**
     * @brief Computes the overall time and determines the last equipped tool according to the actions the robot is capable to
     * 
     * @param _action_queue the action queue
     */
    void
    compute_overall_time_and_determine_last_tool(std::queue<robot_action> _action_queue);

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
     * @brief Resets fields for displayed action and ingredients to "None" and updates the corresponding information nodes
     * 
     */
    void
    reset_in_process_fields();

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
     * @brief Passes the time for the current action
     * 
     * @param _server the server instance from which this method is called
     * @param _data the data passed to the scheduling call
     */
    static void
    pass_time(UA_Server* _server, void* _data);

    /**
     * @brief Callback to indicate the current action completion.
     * 
     */
    void
    action_performed();

    /**
     * @brief Timed callback to indicate retooling completion and to update the current tool and overall time.
     * 
     * @param _server the server instance from which this method is called
     * @param _data the robot instance passed to the scheduling call
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