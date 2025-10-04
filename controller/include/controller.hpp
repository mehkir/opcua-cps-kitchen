// TODO: Add header doxygen
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
#include <condition_variable>
#include <atomic>
#include <functional>
#include <unistd.h>
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
#include "discovery_util.hpp"

using namespace cps_kitchen;

typedef std::function<void(position_t)> mark_robot_for_removal_callback_t; /**< the callback declaration to mark robots for removal. */

/**
 * @brief Remote robot client to monitor kitchen robot attributes.
 * 
 */
struct remote_robot {
    private:
        UA_Client* client_; /**< the OPC UA remote robot client pointer. */
        std::string endpoint_; /**< the endpoint address. */
        const position_t position_; /**< the position on the conveyor belt. */
        std::unordered_set<std::string> capabilities_; /**< the capabilites. */
        mark_robot_for_removal_callback_t mark_robot_for_removal_callback_; /**< the callback to mark robots for removal. */
        std::unordered_map<std::string, UA_NodeId> attribute_id_map_; /**< the map holding the robot's attribute node ids. */
        robot_tool last_equipped_tool_; /**< the last equipped tool. */
        duration_t overall_time_; /**< the total time the robot will be in use. */
        std::atomic<bool> running_; /**< flag to indicate whether the client thread should run. */
        std::thread client_iterate_thread_; /**< the client iteration thread. */
        std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */

    public:
        /**
         * @brief Construct a new remote robot object
         * 
         * @param _endpoint the robot's endpoint url.
         * @param _position the position of the remote robot at the conveyor.
         * @param _capabilities the capabilities.
         * @param _mark_robot_for_removal_callback the mark for removal callback.
         */
        remote_robot(std::string _endpoint, position_t _position, std::unordered_set<std::string> _capabilities, mark_robot_for_removal_callback_t _mark_robot_for_removal_callback) :  endpoint_(_endpoint), position_(_position), capabilities_(_capabilities), client_(nullptr), running_(true), mark_robot_for_removal_callback_(_mark_robot_for_removal_callback) {
            client_connection_establisher robot_connection_establisher;
            bool connected = robot_connection_establisher.establish_connection(client_, endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error establishing robot client session for position %d (async)", position_);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            attribute_id_map_[OVERALL_TIME] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, OVERALL_TIME);
            if (UA_NodeId_equal(&attribute_id_map_[OVERALL_TIME], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, OVERALL_TIME);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            node_value_subscriber nv_subscriber;
            UA_StatusCode status = nv_subscriber.subscribe_node_value(client_, attribute_id_map_[OVERALL_TIME], overall_time_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, OVERALL_TIME, position_);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            attribute_id_map_[LAST_EQUIPPED_TOOL] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, LAST_EQUIPPED_TOOL);
            if (UA_NodeId_equal(&attribute_id_map_[LAST_EQUIPPED_TOOL], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, LAST_EQUIPPED_TOOL);
                mark_robot_for_removal_callback_(position_);
                return;
            }
            status = nv_subscriber.subscribe_node_value(client_, attribute_id_map_[LAST_EQUIPPED_TOOL], last_equipped_tool_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, LAST_EQUIPPED_TOOL, position_);
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
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the robot client iterate thread at position %d", __FUNCTION__, position_);
                running_ = false;
                mark_robot_for_removal_callback_(position_);
                return;
            }
        }

        /**
         * @brief Destroys the remote robot object.
         * 
         */
        ~remote_robot() {
            running_ = false;
            if (client_iterate_thread_.joinable())
                client_iterate_thread_.join();
            UA_Client_delete(client_);
        }

        /**
         * @brief Returns the robot's endpoint.
         * 
         * @return std::string the endpoint url.
         */
        std::string
        get_endpoint() const {
            return endpoint_;
        }

        /**
         * @brief Returns the remote robot's position.
         * 
         * @return position_t the remote robot position.
         */
        position_t
        get_position() const {
            return position_;
        }

        /**
         * @brief Indicates if a robot is capable to perform the given action.
         * 
         * @param _capability the action to check whether it can be performed.
         * @return true if the remote is capable to perform the action.
         * @return false if the remote is not capable to perform the action.
         */
        bool
        is_capable_to(std::string _capability) const {
            return capabilities_.find(_capability) != capabilities_.end();
        }

        /**
         * @brief Returns the remote robot's last equipped tool.
         * 
         * @return robot_tool the last equipped tool.
         */
        robot_tool
        get_last_equipped_tool() const {
            return last_equipped_tool_;
        }

        /**
         * @brief Returns the remote robot's overall time.
         * 
         * @return duration_t the remote robot's overall time.
         */
        duration_t
        get_overall_time() const {
            return overall_time_;
        }

        /**
         * @brief The overall time changed callback for the subscription.
         * 
         * @param _client the client issuing the subscription.
         * @param _sub_id server-assigned subscription id that delivered this notification.
         * @param _sub_context user-defined context data passed when creating the subscription.
         * @param _mon_id server-assigned MonitoredItemId that produced the data change.
         * @param _mon_context user-defined context data passed when creating the monitored item.
         * @param _value the reported UA_DataValue
         */
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
                    self->mark_robot_for_removal_callback_(self->position_);
                    return;
                }
                self->overall_time_ = *(UA_UInt32*) _value->value.data;
                // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's overall time at position %d is %ld", __FUNCTION__, self->position_, self->overall_time_);
        }

        /**
         * @brief The last equipped tool changed callback for the subscription.
         * 
         * @param _client the client issuing the subscription.
         * @param _sub_id server-assigned subscription id that delivered this notification.
         * @param _sub_context user-defined context data passed when creating the subscription.
         * @param _mon_id server-assigned MonitoredItemId that produced the data change.
         * @param _mon_context user-defined context data passed when creating the monitored item.
         * @param _value the reported UA_DataValue.
         */
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
                    self->mark_robot_for_removal_callback_(self->position_);
                    return;
                }
                self->last_equipped_tool_ = *(robot_tool*) _value->value.data;
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's last equipped tool at position %d is %s", __FUNCTION__, self->position_, robot_tool_to_string(self->last_equipped_tool_));
        }
};

class controller {
private:
    /* controller related member variables. */
    UA_Server* server_; /**< the OPC UA controller server pointer. */
    object_type_node_inserter controller_type_inserter_; /**< the controller type insert for adding the controller's methods and attributes to the address space. */
    std::atomic<bool> running_; /**< flag to indicate whether the server thread should run. */
    std::thread server_iterate_thread_; /**< the server iteration thread. */
    discovery_util discovery_util_; /**< the discovery utility. */
    /* robot related member variables. */
    std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>> position_remote_robot_map_; /**< the map holding the remote robot instances. */
    std::unordered_set<position_t> robots_to_be_removed_; /**< the set holding robots to be removed. */
    std::mutex mark_for_removal_mutex_; /**< the mark for removal mutex for synchronizing the to be removed set. */
    /* recipe related member variables. */
    recipe_parser recipe_parser_; /**< the recipe parser. */
 
    /**
     * @brief Extracts the received robot registration parameters.
     * 
     * @param _server the server instance from which this method is called.
     * @param _session_id the client session id.
     * @param _session_context user-defined context data passed via the access control/plugin.
     * @param _method_id the node id of this method.
     * @param _method_context user-defined context data passed to the method node.
     * @param _object_id node id of the object or object type on which the method is called (the “parent” that hasComponent to the method).
     * @param _object_context user-defined context data passed to that object/ObjectType node. Use for instance-specific state.
     * @param _input_size the count of the input parameters.
     * @param _input the input pointer of the input parameters.
     * @param _output_size the allocated output size.
     * @param _output the output pointer to store return parameters.
     * @return UA_StatusCode the status code.
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
     * @param _endpoint the robot's endpoint url.
     * @param _position the position of the remote robot.
     * @param _remote_robot_capabilities the capabilities of the remote robot.
     * @param _output the output pointer to store return parameters.
     */
    void
    handle_robot_registration(std::string _endpoint, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities, UA_Variant* _output);

    /**
     * @brief Extracts the received robot and recipe parameters.
     * 
     * @param _server the server instance from which this method is called.
     * @param _session_id the client session id.
     * @param _session_context user-defined context data passed via the access control/plugin.
     * @param _method_id the node id of this method.
     * @param _method_context user-defined context data passed to the method node.
     * @param _object_id node id of the object or object type on which the method is called (the “parent” that hasComponent to the method).
     * @param _object_context user-defined context data passed to that object/ObjectType node. Use for instance-specific state.
     * @param _input_size the count of the input parameters.
     * @param _input the input pointer of the input parameters.
     * @param _output_size the allocated output size.
     * @param _output the output pointer to store return parameters.
     * @return UA_StatusCode the status code.
     */
    static UA_StatusCode
    choose_next_robot(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Chooses the next suitable robot.
     * 
     * @param _recipe_id the recipe id of the partial finished order.
     * @param _processed_steps the steps until the recipe is processed.
     * @param _output the output pointer to store return parameters.
     */
    void
    handle_next_robot_request(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_Variant* _output);

    /**
     * @brief Returns a suitable robot for the given recipe ID starting from the next step to be processed.
     * 
     * @param _recipe_id the recipe ID.
     * @param _processed_steps the steps until the recipe is processed.
     * @return remote_robot* the suitable robot.
     */
    remote_robot*
    find_suitable_robot(recipe_id_t _recipe_id, UA_UInt32 _processed_steps);

    /**
     * @brief Marks a remote robot for removal.
     * 
     * @param _position the position of the remote robot to mark for removal.
     */
    void
    mark_robot_for_removal(position_t _position); 

    /**
     * @brief Removes all marked robots from the controller.
     * 
     */
    void
    remove_marked_robots();

    /**
     * @brief Helper method for incrementing or decrementing a numerical UA_UINT32 attribute node (increments by default).
     * 
     * @param _attribute_name the attribute name.
     * @param _increment indicator for incrementing or decrementing.
     * @return UA_StatusCode the status code indicating whether incrementing/decrementing succeeded.
     */
    UA_StatusCode
    increment_or_decrement_counter_node(std::string _attribute_name, bool increment = true);

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