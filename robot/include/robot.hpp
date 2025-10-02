/**
 * @file robot.hpp
 * @brief OPC UA kitchen robot server and client logic (public interface and documentation).
 *
 * This header declares the kitchen robot agent which exposes an OPC UA server, registers
 * itself to a discovery server, communicates with the controller and the conveyor via OPC UA
 * method calls, and processes cooking "recipes" as sequences of robot actions.
 *
 * The implementation is multithreaded: the robot hosts its own server iterate loop, runs
 * a worker to progress actions over time using Boost.Asio timers, and maintains client
 * connections to external services.
 */
#ifndef ROBOT_HPP
#define ROBOT_HPP

#include <open62541/server.h>
#include <open62541/client.h>
#include <thread>
#include <queue>
#include <boost/asio.hpp>
#include <atomic>

#include "method_node_caller.hpp"
#include "types.hpp"
#include "robot_tool.hpp"
#include "recipe_parser.hpp"
#include "capability_parser.hpp"
#include "object_type_node_inserter.hpp"
#include "node_browser_helper.hpp"
#include "discovery_util.hpp"

using namespace cps_kitchen;

/**
 * @brief An order object to track incoming orders until they get processed
 * 
 */
struct order {
    private:
        recipe_id_t recipe_id_; /**< the recipe id of the dish */
        UA_UInt32 overall_processed_steps_; /**< the already processed steps on the dish */
        UA_UInt32 overall_processing_steps_; /**< the overall steps to be processed on the dish */
        UA_UInt32 processable_steps_; /**< the processable steps on the current robot */
        std::queue<robot_action> action_queue_; /**< the open actions for the dish to be finished w/o the actions already performed */
    public:
        order(recipe_id_t _recipe_id, UA_UInt32 _overall_processed_steps, UA_UInt32 _overall_processing_steps, UA_UInt32 _processable_steps, std::queue<robot_action> _action_queue) :
            recipe_id_(_recipe_id), overall_processed_steps_(_overall_processed_steps), overall_processing_steps_(_overall_processing_steps), processable_steps_(_processable_steps), action_queue_(_action_queue) {
        }

        /**
         * @brief Returns the recipe id
         * 
         * @return recipe_id_t the recipe id
         */
        recipe_id_t get_recipe_id() const {
            return recipe_id_;
        }

        /**
         * @brief Returns the overall processed steps
         * 
         * @return UA_UInt32 the count of the overall processed steps
         */
        UA_UInt32 get_overall_processed_steps() const {
            return overall_processed_steps_;
        }

        /**
         * @brief Returns the overall processing steps
         * 
         * @return UA_UInt32 Returns the overall processing steps in total
         */
        UA_UInt32 get_overall_processing_steps() const {
            return overall_processing_steps_;
        }

        /**
         * @brief Returns the processable steps the current robot is able to perform according to his capabilities
         * 
         * @return UA_UInt32 the count of the porcessable steps
         */
        UA_UInt32 get_processable_steps() const {
            return processable_steps_;
        }

        /**
         * @brief Returns the open actions for the order to be finished
         * 
         * @return std::queue<robot_action> the open actions to be done for this order
         */
        std::queue<robot_action> get_action_queue() const {
            return action_queue_;
        }
};

class robot {

private:
    /* robot related member variables */
    UA_Server* server_; /**< the OPC UA robot server pointer */
    position_t position_; /**< the robot's position on the conveyor belt */
    std::string robot_uri_; /**< the robot's uniform resource identifier */
    UA_String server_endpoint_; /**< the robot's endpoint address */
    object_type_node_inserter robot_type_inserter_; /**< the robot type inserter for adding the robot's attributes and methods in the address space */
    robot_tool current_tool_; /**< the current tool the robot is equipped with */
    std::queue<order> order_queue_; /**< the queue holding all the assigned orders */
    duration_t current_action_duration_; /**< the current action duration */
    std::queue<robot_action> action_queue_in_process_; /**< the current actions in process */
    bool preparing_dish_; /**< flag to indicate whether the robot is busy preparing a dish */
    bool is_dish_finished_; /**< flag to indicate whether the robot is holding a completed dish or partially finished dish */
    std::atomic<bool> running_; /**< flag to indicate whether the server and client threads should run */
    std::atomic<bool> pending_pickup_; /**< flag to indicate whether there is a pending pickup for an sucessfully sent notifcation to the conveyor */
    discovery_util discovery_util_; /**< the discovery utility */
    std::thread server_iterate_thread_; /**< the server iteration thread */
    recipe_parser recipe_parser_; /**< the recipe parser */
    capability_parser capability_parser_; /**< the capability parser */
    std::unordered_map<std::string, object_method_info> method_id_map_; /**< the map holding the node ids of client methods */
    std::thread worker_thread_; /**< the worker thread for preparing dishes */
    boost::asio::io_context io_context_; /**< the io context managing the worker thread */
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type, void, void> work_guard_; /**< the work guard for the io_context_ */
    boost::asio::steady_timer steady_timer_; /**< the steady timer for simulation action time */
    std::mutex client_mutex_; /**< the mutex to synchronize client method calls */
    std::thread client_iterate_thread_; /**< the client iteration thread */
    /* controller related member variables */
    UA_Client* controller_client_; /**< the OPC UA controller client pointer */
    /* conveyor related member variables */
    UA_Client* conveyor_client_; /**< the OPC UA conveyor client pointer */
    std::condition_variable conveyor_connected_condition_; /**< the condition variable to wait for the conveyor connection to be restored */

    /**
     * @brief Callback called after controller received robot registration. Extracts the controller output values and idicates whether the regeistration was successful.
     * 
     * @param _output_size the count of returned output values
     * @param _output the variant containing the output values
     */
    void
    register_robot_called(size_t _output_size, UA_Variant* _output);

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
     * @return UA_StatusCode the status code
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
     * @param _overall_processed_steps the overall processed steps of the recipe ID so far
     */
    void
    handle_receive_task(recipe_id_t _recipe_id, UA_UInt32 _overall_processed_steps);

    /**
     * @brief Cooks the next order in the order queue
     * 
     */
    void
    cook_next_order();

    /**
     * @brief Computes the overall time and determines the last equipped tool according to the actions the robot is capable to
     * and the processable steps count
     * 
     * @param _action_queue the action queue
     * @return UA_UInt32 the processable steps count
     */
    UA_UInt32
    compute_overall_time_and_determine_last_tool(std::queue<robot_action> _action_queue);

    /**
     * @brief Initiates the handover of the finished order.
     * 
     * @param _server the server instance from which this method is called
     * @param _session_id the client session id
     * @param _session_context user-defined context data passed via the access control/plugin
     * @param _method_id the node id of this method
     * @param _method_context user-defined context data passed to the method node
     * @param _object_id node id of the object or object type on which the method is called (the “parent” that hasComponent to the method).
     * @param _object_context user-defined context data passed to that object/ObjectType node. Use for instance-specific state.
     * @param _input_size the count of the input parameters
     * @param _input the input pointer of the input parameters
     * @param _output_size the allocated output size
     * @param _output the output pointer to store return parameters
     * @return UA_StatusCode the status code
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
     * @brief Determines whether there are still open steps or necessary retooling for the current dish in process.
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
     * @brief Callback called after conveyor is notified about finished dish. Extracts the conveyor response and indicates whether the notification is received successfully.
     * 
     * @param _output_size the count of returned output values
     * @param _output the variant containing the output values
     */
    void
    receive_finished_order_notification_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Passes the time for the current action
     * 
     */
    void
    pass_time();

    /**
     * @brief Callback to indicate the current action completion.
     * 
     */
    void
    action_performed();

    /**
     * @brief Timed callback to indicate retooling completion and to update the current tool and overall time.
     * 
     */
    void
    retool();

    /**
     * @brief Joins all started threads.
     * 
     */
    void
    join_threads();

public:
    /**
     * @brief Constructs a new robot object.
     * 
     * @param _position the position of the robot at the conveyor
     */
    robot(position_t _position, std::string _capabilities_file_name);

    /**
     * @brief Destroys the robot object.
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