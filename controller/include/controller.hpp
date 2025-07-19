#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <open62541/server.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <map>
#include <unordered_set>
#include <memory>
#include <random>
#include <condition_variable>
#include <atomic>
#include "node_value_subscriber.hpp"
#include "browsenames.h"
#include "method_node_caller.hpp"
#include "client_connection_establisher.hpp"
#include "types.hpp"
#include "recipe_parser.hpp"
#include "robot_state.hpp"
#include "robot_tool.hpp"
#include "object_type_node_inserter.hpp"
#include "node_browser_helper.hpp"

using namespace cps_kitchen;

struct remote_robot {
    private:
        UA_Client* async_client_;
        UA_Client* sync_client_;
        std::string endpoint_;
        const position_t position_;
        std::unordered_set<std::string> capabilities_;
        std::unordered_map<std::string, UA_NodeId> attribute_id_map_;
        std::unordered_map<std::string, object_method_info> method_id_map_;
        robot_state state_;
        robot_tool last_equipped_tool_;
        duration_t overall_time_;
        bool running_;
        std::thread client_thread_;

    public:
        /**
         * @brief Construct a new remote robot object.
         * 
         * @param _endpoint the robot's endpoint url
         * @param _position the position of the remote robot at the conveyor
         */
        remote_robot(std::string _endpoint, position_t _position, std::unordered_set<std::string> _capabilities) :  endpoint_(_endpoint), position_(_position), capabilities_(_capabilities), async_client_(UA_Client_new()), sync_client_(UA_Client_new()), running_(true) {
            client_connection_establisher cce_async(async_client_);
            bool connected = cce_async.establish_connection(endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error establishing robot client session for position %d (async)", position_);
                running_ = false;
                return;
            }
            attribute_id_map_[OVERALL_TIME] = node_browser_helper().get_attribute_id(async_client_, ROBOT_TYPE, OVERALL_TIME);
            if (UA_NodeId_equal(&attribute_id_map_[OVERALL_TIME], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, OVERALL_TIME);
                running_ = false;
                return;
            }
            node_value_subscriber nv_subscriber;
            UA_StatusCode status = nv_subscriber.subscribe_node_value(async_client_, attribute_id_map_[OVERALL_TIME], overall_time_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, OVERALL_TIME, position_);
                running_ = false;
                return;
            }
            attribute_id_map_[LAST_EQUIPPED_TOOL] = node_browser_helper().get_attribute_id(async_client_, ROBOT_TYPE, LAST_EQUIPPED_TOOL);
            if (UA_NodeId_equal(&attribute_id_map_[LAST_EQUIPPED_TOOL], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, LAST_EQUIPPED_TOOL);
                running_ = false;
                return;
            }
            status = nv_subscriber.subscribe_node_value(async_client_, attribute_id_map_[LAST_EQUIPPED_TOOL], last_equipped_tool_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, LAST_EQUIPPED_TOOL, position_);
                running_ = false;
                return;
            }
            client_connection_establisher cce_sync(sync_client_);
            connected = cce_sync.establish_connection(endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error establishing robot client session for position %d (sync)", position_);
                running_ = false;
                return;
            }
            if ((method_id_map_[RECEIVE_TASK] = node_browser_helper().get_method_id(sync_client_, ROBOT_TYPE, RECEIVE_TASK)) == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECEIVE_TASK);
                running_ = false;
            }
        }

        /**
         * @brief Destroy the remote robot object.
         * 
         */
        ~remote_robot() {
            running_ = false;
            if (client_thread_.joinable())
                client_thread_.join();
            UA_Client_delete(async_client_);
            UA_Client_delete(sync_client_);
        }
        
        /**
         * @brief Starts the housekeeping thread
         * 
         */
        void start_thread() {
            client_thread_ = std::thread([this]() {
                while(running_) {
                    UA_StatusCode status = UA_Client_run_iterate(async_client_, 1000);
                    if (status != UA_STATUSCODE_GOOD) {
                        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot client at position %d (%s)", __FUNCTION__, position_, UA_StatusCode_name(status));
                        running_ = false;
                    }
                }
            });
        }

        /**
         * @brief Returns the robot's endpoint
         * 
         * @return std::string the endpoint url
         */
        std::string
        get_endpoint() const {
            return endpoint_;
        }

        /**
         * @brief Returns the remote robot's position.
         * 
         * @return position_t the remote robot position
         */
        position_t
        get_position() const {
            return position_;
        }

        /**
         * @brief Indicates if a robot is capable to perform the given action
         * 
         * @param _capability the action to check whether it can be performed
         * @return true if the remote is capable to perform the action
         * @return false if the remote is not capable to perform the action
         */
        bool
        is_capable_to(std::string _capability) const {
            return capabilities_.find(_capability) != capabilities_.end();
        }

        /**
         * @brief Returns the remote robot's last equipped tool
         * 
         * @return robot_tool the last equipped tool
         */
        robot_tool
        get_last_equipped_tool() const {
            return last_equipped_tool_;
        }

        /**
         * @brief Returns the remote robot's overall time
         * 
         * @return duration_t the remote robot's overall time
         */
        duration_t
        get_overall_time() const {
            return overall_time_;
        }

        /**
         * @brief Instructs the remote robot to process a dish.
         * 
         * @param _recipe_id the recipe ID of the dish
         * @param _processed_steps the processed steps of the recipe ID so far
         * @param _output_size the count of returned output values
         * @param _output the variant containing the output values
         * 
         * @return UA_StatusCode the status whether the method call was successful
         */
        UA_StatusCode
        instruct(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "INSTRUCTIONS: Instruct robot on position %d to cook recipe %d from step %d", position_, _recipe_id, _processed_steps);
            method_node_caller receive_robot_task_caller;
            receive_robot_task_caller.add_scalar_input_argument(&_recipe_id, UA_TYPES_UINT32);
            receive_robot_task_caller.add_scalar_input_argument(&_processed_steps, UA_TYPES_UINT32);
            object_method_info omi = method_id_map_[RECEIVE_TASK];
            if (omi == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECEIVE_TASK);
                running_ = false;
                return UA_STATUSCODE_BAD;
            }
            UA_StatusCode status = receive_robot_task_caller.call_method_node(sync_client_, omi.object_id_, omi.method_id_, _output_size, _output);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling instruct method (%s)", __FUNCTION__, UA_StatusCode_name(status));
                running_ = false;
                return UA_STATUSCODE_BAD;
            }
            return status;
        }

        static void
        overall_time_changed(UA_Client* _client, UA_UInt32 _sub_id, void* _sub_context,
            UA_UInt32 _mon_id, void* _mon_context, UA_DataValue* _value) {
                if(_mon_context == NULL) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Monitor context is NULL", __FUNCTION__);
                    return;
                }
                remote_robot* self = static_cast<remote_robot*>(_mon_context);
                if (!UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT32])) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
                    return;
                }
                self->overall_time_ = *(duration_t*) _value->value.data;
                // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's overall time at position %d is %ld", __FUNCTION__, self->position_, self->overall_time_);
        }

        static void
        last_equipped_tool_changed(UA_Client* _client, UA_UInt32 _sub_id, void* _sub_context,
            UA_UInt32 _mon_id, void* _mon_context, UA_DataValue* _value) {
                if(_mon_context == NULL) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Monitor context is NULL", __FUNCTION__);
                    return;
                }
                remote_robot* self = static_cast<remote_robot*>(_mon_context);
                if (!UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT32])) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
                    return;
                }
                self->last_equipped_tool_ = *(robot_tool*) _value->value.data;
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's last equipped tool at position %d is %s", __FUNCTION__, self->position_, robot_tool_to_string(self->last_equipped_tool_));
        }
};

class controller {
private:
    /* controller related member variables */
    UA_Server* server_;
    port_t port_;
    object_type_node_inserter controller_type_inserter_;
    std::atomic<bool> running_;
    std::thread server_iterate_thread_;
    std::mutex discovery_mutex_;
    std::condition_variable discovery_cv_;
    std::thread discovery_thread_;
    /* robot related member variables */
    std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>> position_remote_robot_map_;
    /* recipe related member variables */
    recipe_parser recipe_parser_;
    /* random distribution */
    std::random_device random_device_;
    std::mt19937 mersenne_twister_;
    std::uniform_int_distribution<std::uint32_t> uniform_int_distribution_;

    /**
     * @brief Extracts the received robot registration parameters.
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
     * @return UA_StatusCode the status whether the registration was successful
     */
    static UA_StatusCode
    register_robot(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Registers a remote robot.
     * 
     * @param _endpoint the robot's endpoint url
     * @param _position the position of the remote robot
     * @param _remote_robot_capabilities the capabilities of the remote robot
     * @param _output the output pointer to store return parameters
     */
    void
    handle_robot_registration(std::string _endpoint, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities, UA_Variant* _output);

    /**
     * @brief Extracts the received robot and recipe parameters.
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
     * @return UA_StatusCode the status whether a suitable robot was found
     */
    static UA_StatusCode
    choose_next_robot(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Chooses the next suitable robot
     * 
     * @param _position the position of the remote robot
     * @param _recipe_id the recipe id of the partial finished order
     * @param _processed_steps the steps until the recipe is processed
     * @param _output the output pointer to store return parameters
     */
    void
    handle_next_robot_request(position_t _position, recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_Variant* _output);

    /**
     * @brief Returns a suitable robot for the given recipe ID starting from the next step to be processed
     * 
     * @param _recipe_id the recipe ID
     * @param _processed_steps the steps until the recipe is processed
     * @return remote_robot* the suitable robot
     */
    remote_robot*
    find_suitable_robot(recipe_id_t _recipe_id, UA_UInt32 _processed_steps);

    /**
     * @brief Places a random order.
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
     * @return UA_StatusCode TODO
     */
    static UA_StatusCode
    place_random_order(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Handles the random order request.
     * 
     * @param _output the output pointer to store return parameters
     */
    void
    handle_random_order_request(UA_Variant* _output);

    /**
     * @brief Extracts the returned robot state parameters.
     * 
     * @param _output_size the count of returned output values
     * @param _output the variant containing the output values
     */
    void
    receive_robot_task_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Joins all started threads.
     * 
     */
    void
    join_threads();

public:
    /**
     * @brief Construct a new controller object.
     * 
     */
    controller();

    /**
     * @brief Destroy the controller object.
     * 
     */
    ~controller();

    /**
     * @brief Checks if initialization was successful and joins all started threads.
     * 
     */
    void
    start();

    /**
     * @brief Stops the controller and shuts it down.
     * 
     */
    void
    stop();
};

#endif // CONTROLLER_HPP