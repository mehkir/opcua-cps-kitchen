#ifndef KITCHEN_HPP
#define KITCHEN_HPP

#define REMOTE_ROBOT_INSTANCE_NAME_PREFIX "RemoteKitchenRobot"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/client.h>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <random>
#include <functional>

#include "object_type_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "node_browser_helper.hpp"
#include "discovery_util.hpp"
#include "browsenames.h"
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "types.hpp"

using namespace cps_kitchen;

typedef std::function<void(position_t)> mark_robot_for_removal_callback_t;

struct remote_robot {
    private:
        UA_Client* client_;
        std::string endpoint_;
        position_t position_;
        std::atomic<bool> running_;
        object_type_node_inserter& remote_robot_type_inserter_;
        mark_robot_for_removal_callback_t mark_robot_for_removal_callback_;
        std::thread client_iterate_thread_;
        std::mutex client_mutex_;
        std::unordered_map<std::string, object_method_info> method_id_map_;
        std::unordered_map<std::string, UA_NodeId> attribute_id_map_;
    public:
        /**
         * @brief Setup the remote robot object type
         * 
         * @param _remote_robot_type_inserter the remote robot type inserter
         * @param _conveyor the conveyor server
         * @return UA_StatusCode the status code
         */
        static UA_StatusCode setup_remote_robot_object_type(object_type_node_inserter& _remote_robot_type_inserter, UA_Server* _kitchen) {
            UA_StatusCode status;
            /* Add attributes */
            status = _remote_robot_type_inserter.add_attribute(REMOTE_ROBOT_TYPE, POSITION);
            status |= _remote_robot_type_inserter.add_attribute(REMOTE_ROBOT_TYPE, CONNECTIVITY);
            /* Add remote robot type constructor */
            status |= _remote_robot_type_inserter.add_object_type_constructor(_kitchen, _remote_robot_type_inserter.get_object_type_id(REMOTE_ROBOT_TYPE));
            return status;
        }

        /**
         * @brief Construct a new remote robot object.
         * 
         * @param _endpoint the remote robot's endpoint url
         * @param _position the remote robot's position
         * @param _remote_robot_type_inserter the remote robot type inserter
         * @param _mark_robot_for_removal_callback the mark robot for removal callback
         */
        remote_robot(std::string _endpoint, UA_UInt32 _position, object_type_node_inserter& _remote_robot_type_inserter, mark_robot_for_removal_callback_t _mark_robot_for_removal_callback) : client_(nullptr), endpoint_(_endpoint), position_(_position), running_(true), remote_robot_type_inserter_(_remote_robot_type_inserter), mark_robot_for_removal_callback_(_mark_robot_for_removal_callback) {
            client_connection_establisher robot_client_connection_establisher;
            bool connected = robot_client_connection_establisher.establish_connection(client_, endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error establishing robot client session", __FUNCTION__);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            /* Get the position attribute id*/
            attribute_id_map_[POSITION] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, POSITION);
            if (UA_NodeId_equal(&attribute_id_map_[POSITION], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            /* Subscribe to position changes */
            node_value_subscriber nv_subscriber;
            UA_StatusCode status = nv_subscriber.subscribe_node_value(client_, attribute_id_map_[POSITION], position_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s", __FUNCTION__, POSITION);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            /* Set connectvitiy */
            status = remote_robot_type_inserter_.set_scalar_attribute(remote_robot_instance_name(position_), CONNECTIVITY, &connected, UA_TYPES_BOOLEAN);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting remote robot connectivity attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
                mark_robot_for_removal_callback_(position_);
                return;
            }
            /* Get receive task method id */
            if ((method_id_map_[RECEIVE_TASK] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, RECEIVE_TASK)) == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECEIVE_TASK);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            try {
                client_iterate_thread_ = std::thread([this]() {
                    while(running_) {
                        {
                            std::lock_guard<std::mutex> lock(client_mutex_);
                            UA_StatusCode status = UA_Client_run_iterate(client_, 1);
                            if (status != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot client at position %d (%s)", __FUNCTION__, position_, UA_StatusCode_name(status));
                                running_ = false;
                                mark_robot_for_removal_callback_(position_);
                                return;
                            }
                        }
                        if (usleep(1*1000)) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at robot client iterate sleep", __FUNCTION__);
                            running_ = false;
                            mark_robot_for_removal_callback_(position_);
                            return;
                        }
                        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Starting the next client iterate", __FUNCTION__);
                    }
                });
            } catch (...) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running the robot client iterate thread at position %d", __FUNCTION__, position_);
                running_ = false;
                mark_robot_for_removal_callback_(position_);
                return;
            }
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
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                status = receive_robot_task_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling instruct method (%s)", __FUNCTION__, UA_StatusCode_name(status));
                    running_ = false;
                    mark_robot_for_removal_callback_(position_);
                    return UA_STATUSCODE_BAD;
                }
            }
            return status;
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

        static void
        position_changed(UA_Client* _client, UA_UInt32 _sub_id, void* _sub_context,
            UA_UInt32 _mon_id, void* _mon_context, UA_DataValue* _value) {
            if(_mon_context == NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Monitor context is NULL", __FUNCTION__);
                return;
            }
            remote_robot* self = static_cast<remote_robot*>(_mon_context);
            if (!UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT32])) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
                self->mark_robot_for_removal_callback_(self->position_);
                return;
            }
            self->position_ = *(position_t*)_value->value.data;
            UA_StatusCode status;
            if ((status = self->remote_robot_type_inserter_.set_scalar_attribute(remote_robot_instance_name(self->position_).c_str(), POSITION, &self->position_, UA_TYPES_UINT32)) != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting %s information node", __FUNCTION__, POSITION, UA_StatusCode_name(status));
                self->mark_robot_for_removal_callback_(self->position_);
            }
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's position updated/changed to %d ", __FUNCTION__, self->position_);
        }

        static std::string remote_robot_instance_name(position_t _position) {
            return REMOTE_ROBOT_INSTANCE_NAME_PREFIX + std::to_string(_position);
        }

        /**
         * @brief Destroy the remote robot object.
         * 
         */
        ~remote_robot() {
            running_ = false;
            if (client_iterate_thread_.joinable())
                client_iterate_thread_.join();
            UA_Client_delete(client_);
        }
};

class kitchen {

private:
    /* kitchen related member variables */
    UA_Server* server_;
    std::string kitchen_uri_;
    object_type_node_inserter kitchen_type_inserter_;
    std::atomic<bool> running_;
    discovery_util discovery_util_;
    std::unordered_map<std::string, object_method_info> method_id_map_;
    std::thread server_iterate_thread_;
    std::mutex client_mutex_;
    std::thread client_iterate_thread_;
    /* remote robot related member variables */
    std::unordered_map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_;
    std::unordered_set<position_t> robots_to_be_removed_;
    object_type_node_inserter remote_robot_type_inserter_;
    std::thread cyclic_remote_robot_discovery_thread_;
    std::mutex remote_robot_discovery_mutex_;
    std::mutex mark_for_removal_mutex_;
    uint32_t robot_count_;
    std::condition_variable remote_robot_discovery_cv;
    /* controller related member variables */
    UA_Client* controller_client_;
    object_type_node_inserter remote_controller_type_inserter_;
    /* conveyor related member variables */
    UA_Client* conveyor_client_;
    object_type_node_inserter remote_conveyor_type_inserter_;
    /* random distribution */
    std::random_device random_device_;
    std::mt19937 mersenne_twister_;
    std::uniform_int_distribution<std::uint32_t> uniform_int_distribution_;

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
     * @return UA_StatusCode the status code in the response
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
     * @return true if instruction suceeded
     * @return false if instruction failed
     */
    bool
    receive_robot_task_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Extracts the returned remote robot parameters
     * 
     * @param _output_size the count of returned output values
     * @param _output the variant containing the output values
     * @return remote_robot* the pointer of the remote robot extracted by the returned parameters
     */
    remote_robot*
    choose_next_robot_called(size_t _output_size, UA_Variant *_output);

    /**
     * @brief Helper method for incrementing X_ORDERS attribute nodes
     * 
     * @param _attribute_name the attribute name
     * @return UA_StatusCode the status code indicating if incrementing succeeded
     */
    UA_StatusCode
    increment_orders_counter(std::string _attribute_name);

    /**
     * @brief Marks a remote robot for removal.
     * 
     * @param _position the position of the remote robot to mark for removal
     */
    void
    mark_robot_for_removal(position_t _position); 

    /**
     * @brief Removes all marked robots from the kitchen.
     * 
     */
    void
    remove_marked_robots();

    /**
     * @brief Joins all started threads.
     * 
     */
    void
    join_threads();

public:
    kitchen(uint32_t _robot_count);
    ~kitchen();

    /**
     * @brief Starts the kitchen and joins all started threads.
     * 
     */
    void
    start();

    /**
     * @brief Stops the kitchen and shuts it down.
     * 
     */
    void
    stop();
};

#endif // KITCHEN_HPP