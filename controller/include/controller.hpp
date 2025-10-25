/**
 * @file controller.hpp
 * @brief OPC UA based production controller that monitors robot attributes to appoint the next suitable robot on request.
 *
 * @details
 * The controller hosts an OPC UA server (open62541) that monitors robot attributes to appoint a suitable robot
 * for the next preparation steps of a recipe requested by the Kitchen- and Conveyor-Agent.
 */
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
#include <boost/asio.hpp>
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
#include "mape.hpp"
#include "information_node_reader.hpp"

using namespace cps_kitchen;

typedef std::function<void(position_t)> mark_robot_for_removal_callback_t; /**< the callback declaration to mark robots for removal. */
typedef std::function<void(position_t, position_t)> position_swapped_callback_t; /**< the callback declaration to notify about position change. */
typedef std::function<void(position_t)> capabilities_reconfigured_callback_t; /**< the callback declaration to notify about position change. */

/**
 * @brief Remote robot client to monitor kitchen robot attributes.
 * 
 */
struct remote_robot {
    private:
        UA_Client* client_; /**< the OPC UA remote robot client pointer. */
        std::string endpoint_; /**< the endpoint address. */
        std::atomic<position_t> position_; /**< the position on the conveyor belt. */
        std::unordered_set<std::string> capabilities_; /**< the capabilites. */
        mark_robot_for_removal_callback_t mark_robot_for_removal_callback_; /**< the callback to mark robots for removal. */
        position_swapped_callback_t position_swapped_callback_; /**< the callback to notify about position change. */
        capabilities_reconfigured_callback_t capabilities_reconfigured_callback_; /**< the callback to notify about capabilitiy reconfgurations. */
        std::unique_ptr<node_value_subscriber> nv_subscriber_; /**< the node value subscriber. */
        std::unordered_map<std::string, UA_NodeId> attribute_id_map_; /**< the map holding the robot's attribute node ids. */
        std::unordered_map<std::string, object_method_info> method_id_map_; /**< the map holding the node ids of client methods. */
        std::atomic<robot_tool> last_equipped_tool_; /**< the last equipped tool. */
        std::atomic<duration_t> overall_time_; /**< the total time the robot will be in use. */
        std::atomic<bool> running_; /**< flag to indicate whether the client thread should run. */
        std::thread client_iterate_thread_; /**< the client iteration thread. */
        std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */
        std::atomic<bool> adaptivity_is_pending_; /**< flag to indicate whether adaptivity is pending. */
        bool initial_position_subscription_; /**< flag to indicate initial position subscription notification. */
        bool initial_capabilities_subscription_; /**< flag to indicate initial capabilities subscription notification. */

    public:
        /**
         * @brief Constructs a new remote robot object.
         * 
         * @param _endpoint the robot's endpoint url.
         * @param _position the position of the remote robot at the conveyor.
         * @param _capabilities the capabilities.
         * @param _mark_robot_for_removal_callback the mark for removal callback.
         * @param _position_swapped_callback the position swapped callback.
         */
        remote_robot(std::string _endpoint, position_t _position, std::unordered_set<std::string> _capabilities,
                    mark_robot_for_removal_callback_t _mark_robot_for_removal_callback,
                    position_swapped_callback_t _position_swapped_callback, capabilities_reconfigured_callback_t _capabilities_reconfigured_callback) :
                    endpoint_(_endpoint), position_(_position), capabilities_(_capabilities), client_(nullptr),
                    running_(true), adaptivity_is_pending_(false), mark_robot_for_removal_callback_(_mark_robot_for_removal_callback),
                    position_swapped_callback_(_position_swapped_callback), capabilities_reconfigured_callback_(_capabilities_reconfigured_callback),
                    initial_position_subscription_(true), initial_capabilities_subscription_(true) {
            client_connection_establisher robot_connection_establisher;
            bool connected = robot_connection_establisher.establish_connection(client_, endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error establishing robot client session for position %d (async)", position_.load());
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            attribute_id_map_[AVAILABILITY] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, AVAILABILITY);
            if (UA_NodeId_equal(&attribute_id_map_[AVAILABILITY], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, AVAILABILITY);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            attribute_id_map_[POSITION] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, POSITION);
            if (UA_NodeId_equal(&attribute_id_map_[POSITION], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            nv_subscriber_ = std::make_unique<node_value_subscriber>(client_);
            UA_StatusCode status = nv_subscriber_->subscribe_node_value(attribute_id_map_[POSITION], position_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, POSITION, position_.load());
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            attribute_id_map_[CAPABILITIES] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, CAPABILITIES);
            if (UA_NodeId_equal(&attribute_id_map_[CAPABILITIES], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, CAPABILITIES);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            status = nv_subscriber_->subscribe_node_value(attribute_id_map_[CAPABILITIES], capabilities_reconfigured, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, CAPABILITIES, position_.load());
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            attribute_id_map_[OVERALL_TIME] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, OVERALL_TIME);
            if (UA_NodeId_equal(&attribute_id_map_[OVERALL_TIME], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, OVERALL_TIME);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            status = nv_subscriber_->subscribe_node_value(attribute_id_map_[OVERALL_TIME], overall_time_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, OVERALL_TIME, position_.load());
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            attribute_id_map_[LAST_EQUIPPED_TOOL] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, LAST_EQUIPPED_TOOL);
            if (UA_NodeId_equal(&attribute_id_map_[LAST_EQUIPPED_TOOL], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, LAST_EQUIPPED_TOOL);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            status = nv_subscriber_->subscribe_node_value(attribute_id_map_[LAST_EQUIPPED_TOOL], last_equipped_tool_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, LAST_EQUIPPED_TOOL, position_.load());
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            if ((method_id_map_[SWITCH_POSITION] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, SWITCH_POSITION)) == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, SWITCH_POSITION);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            if ((method_id_map_[RECONFIGURE] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, RECONFIGURE)) == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECONFIGURE);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            try {
                client_iterate_thread_ = std::thread([this]() {
                    while(running_) {
                        {
                            std::lock_guard<std::mutex> lock(client_mutex_);
                            UA_StatusCode status = UA_Client_run_iterate(client_, 1);
                            if (status != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot client at position %d (%s)", __FUNCTION__, position_.load(), UA_StatusCode_name(status));
                                running_ = false;
                                mark_robot_for_removal_callback_(position_.load());
                                return;
                            }
                        }
                        if (usleep(1*1000)) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at robot client iterate sleep", __FUNCTION__);
                            running_ = false;
                            mark_robot_for_removal_callback_(position_.load());
                            return;
                        }
                        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Starting the next client iterate", __FUNCTION__);
                    }
                });
            } catch (...) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the robot client iterate thread at position %d", __FUNCTION__, position_.load());
                running_ = false;
                mark_robot_for_removal_callback_(position_.load());
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
         * @brief Instructs the remote robot to switch its position to the given one.
         * 
         * @param _new_position the new position to switch to.
         * @param _output_size the count of returned output values.
         * @param _output the variant containing the output values.
         * @return UA_StatusCode the status whether method call was successful.
         */
        UA_StatusCode
        switch_position_to(position_t _new_position, size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "SWITCH POSTION: Instruct robot on position %d to switch to position %d", position_.load(), _new_position);
            method_node_caller switch_robot_position_caller;
            switch_robot_position_caller.add_scalar_input_argument(&_new_position, UA_TYPES_UINT32);
            object_method_info omi = method_id_map_[SWITCH_POSITION];
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                status = switch_robot_position_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling %s method (%s)", __FUNCTION__, SWITCH_POSITION, UA_StatusCode_name(status));
                    running_ = false;
                    mark_robot_for_removal_callback_(position_.load());
                    return UA_STATUSCODE_BAD;
                }
            }
            return status;
        }

        /**
         * @brief Instructs the remote robot to reconfigure its capabilities according to the given profile.
         * 
         * @param _new_capabilities_profile the new capabilities profile.
         * @param _output_size the count of returned output values.
         * @param _output the variant containing the output values.
         * @return UA_StatusCode the status whether method call was successful.
         */
        UA_StatusCode
        reconfigure_capabilities(std::string _new_capabilities_profile, size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RECONFIGURE CAPABILITIES: Instruct robot on position %d to reconfigure capabilities to profile %s", position_.load(), _new_capabilities_profile.c_str());
            method_node_caller reconfigure_robot_caller;
            UA_String new_capabilities_profile = UA_STRING_ALLOC(_new_capabilities_profile.c_str());
            reconfigure_robot_caller.add_scalar_input_argument(&new_capabilities_profile, UA_TYPES_STRING);
            object_method_info omi = method_id_map_[RECONFIGURE];
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                status = reconfigure_robot_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling %s method (%s)", __FUNCTION__, RECONFIGURE, UA_StatusCode_name(status));
                    UA_String_clear(&new_capabilities_profile);
                    running_ = false;
                    mark_robot_for_removal_callback_(position_.load());
                    return UA_STATUSCODE_BAD;
                }
                UA_String_clear(&new_capabilities_profile);
            }
            return status;
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
            return position_.load();
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
            return last_equipped_tool_.load();
        }

        /**
         * @brief Returns the remote robot's overall time.
         * 
         * @return duration_t the remote robot's overall time.
         */
        duration_t
        get_overall_time() const {
            return overall_time_.load();
        }

        /**
         * @brief The position changed callback for the subscription.
         * 
         * @param _client the client issuing the subscription.
         * @param _sub_id server-assigned subscription id that delivered this notification.
         * @param _sub_context user-defined context data passed when creating the subscription.
         * @param _mon_id server-assigned MonitoredItemId that produced the data change.
         * @param _mon_context user-defined context data passed when creating the monitored item.
         * @param _value the reported UA_DataValue.
         */
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
                self->mark_robot_for_removal_callback_(self->position_.load());
                return;
            }
            UA_UInt32 old_position = self->position_.load();
            self->position_.store(*(UA_UInt32*) _value->value.data);
            if (self->initial_position_subscription_) {
                self->initial_position_subscription_ = false;
                return;
            }
            self->position_swapped_callback_(old_position, self->position_.load());
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's position changed from %d to %d", __FUNCTION__, old_position, self->position_.load());
        }

        /**
         * @brief The capabilities reconfigured callback for the subscription.
         * 
         * @param _client the client issuing the subscription.
         * @param _sub_id server-assigned subscription id that delivered this notification.
         * @param _sub_context user-defined context data passed when creating the subscription.
         * @param _mon_id server-assigned MonitoredItemId that produced the data change.
         * @param _mon_context user-defined context data passed when creating the monitored item.
         * @param _value the reported UA_DataValue.
         */
        static void
        capabilities_reconfigured(UA_Client* _client, UA_UInt32 _sub_id, void* _sub_context,
            UA_UInt32 _mon_id, void* _mon_context, UA_DataValue* _value) {
            if(_mon_context == NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Monitor context is NULL", __FUNCTION__);
                return;
            }
            remote_robot* self = static_cast<remote_robot*>(_mon_context);
            if (!UA_Variant_hasArrayType(&_value->value, &UA_TYPES[UA_TYPES_STRING])) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
                self->mark_robot_for_removal_callback_(self->position_.load());
                return;
            }
            std::unordered_set<std::string> remote_robot_capabilities;
            for (size_t i = 0; i < _value->value.arrayLength; i++) {
                UA_String capability = ((UA_String*)_value->value.data)[i];
                remote_robot_capabilities.insert(std::string((char*) capability.data, capability.length));
            }
            self->capabilities_ = remote_robot_capabilities;
            if (self->initial_capabilities_subscription_) {
                self->initial_capabilities_subscription_ = false;
                return;
            }
            self->capabilities_reconfigured_callback_(self->position_.load());
        }
        
        /**
         * @brief The overall time changed callback for the subscription.
         * 
         * @param _client the client issuing the subscription.
         * @param _sub_id server-assigned subscription id that delivered this notification.
         * @param _sub_context user-defined context data passed when creating the subscription.
         * @param _mon_id server-assigned MonitoredItemId that produced the data change.
         * @param _mon_context user-defined context data passed when creating the monitored item.
         * @param _value the reported UA_DataValue.
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
                self->mark_robot_for_removal_callback_(self->position_.load());
                return;
            }
            self->overall_time_.store(*(UA_UInt32*) _value->value.data);
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's overall time at position %d is %ld", __FUNCTION__, self->position_.load(), self->overall_time_);
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
                self->mark_robot_for_removal_callback_(self->position_.load());
                return;
            }
            self->last_equipped_tool_.store(*(robot_tool*) _value->value.data);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's last equipped tool at position %d is %s", __FUNCTION__, self->position_.load(), robot_tool_to_string(self->last_equipped_tool_.load()));
        }

        /**
         * @brief Returns whether the robot is available.
         * 
         * @return true if robot is available.
         * @return false of robot is not available.
         */
        UA_Boolean
        is_available() {
            std::lock_guard<std::mutex> lock(client_mutex_);
            information_node_reader inr;
            if (inr.read_information_node(client_, attribute_id_map_[AVAILABILITY]) != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not read the %s attribute id", __FUNCTION__, AVAILABILITY);
                mark_robot_for_removal_callback_(position_.load());
                return false;
            }
            return *(UA_Boolean*)inr.get_variant()->data;
        }

        /**
         * @brief Set the adaptivity flag.
         */
        void
        set_adaptivity_flag() {
            adaptivity_is_pending_.store(true);
        }

        /**
         * @brief Reset the adaptivity flag.
         */
        void
        reset_adaptivity_flag() {
            adaptivity_is_pending_.store(false);
        }

        /**
         * @brief Returns the adaptivity flag value.
         * 
         * @return true if adaptivity is still pending.
         * @return false if there is no adaptivity running.
         */
        bool
        is_adaptivity_pending() {
            return adaptivity_is_pending_.load();
        }
};

/**
 * @brief Next robot reciver agents to inform 
 * 
 */
struct next_robot_receiver {
    private:
        UA_Client* client_; /**< the OPC UA remote robot client pointer. */
        std::string endpoint_; /**< the endpoint address. */
        std::string type_; /**< the agent type. */
        std::unique_ptr<node_value_subscriber> nv_subscriber_; /**< the node value subscriber. */
        std::unordered_map<std::string, object_method_info> method_id_map_; /**< the map holding the node ids of client methods. */
        std::atomic<bool> running_; /**< flag to indicate whether the client thread should run. */
        std::thread client_iterate_thread_; /**< the client iteration thread. */
        std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */
    public:
};

/**
 * @brief Custom tuple hash functor with golden ratio magic number.
 * 
 */
struct tuple_hash {
    template <typename T1, typename T2>
    std::size_t operator()(const std::tuple<T1, T2>& t) const noexcept {
        std::size_t h1 = std::hash<T1>{}(std::get<0>(t));
        std::size_t h2 = std::hash<T2>{}(std::get<1>(t));
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

/**
 * @brief Tracks whether pair-wise swap is acknowledged by both robots.
 * 
 */
struct swap_state {
    bool ack_from_lower_position = false;
    bool ack_from_greater_position = false;
    bool second_robot_failed = false;
};

using swap_key = std::tuple<UA_UInt32, UA_UInt32>;
class controller {
private:
    /* controller related member variables. */
    UA_Server* server_; /**< the OPC UA controller server pointer. */
    object_type_node_inserter controller_type_inserter_; /**< the controller type insert for adding the controller's methods and attributes to the address space. */
    std::atomic<bool> running_; /**< flag to indicate whether the server thread should run. */
    std::thread server_iterate_thread_; /**< the server iteration thread. */
    discovery_util discovery_util_; /**< the discovery utility. */
    std::thread worker_thread_; /**< the worker thread. */
    boost::asio::io_context io_context_; /**< the io context managing the worker thread. */
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type, void, void> work_guard_; /**< the work guard for the io_context_. */
    std::map<std::string, next_robot_receiver> next_robot_receiver_map_; /**< the map holding the next robot receivers. */
    /* robot related member variables. */
    std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>> position_remote_robot_map_; /**< the map holding the remote robot instances. */
    std::unordered_set<position_t> robots_to_be_removed_; /**< the set holding robots to be removed. */
    /* recipe related member variables. */
    recipe_parser recipe_parser_; /**< the recipe parser. */
    /* mape interface related member variables */
    std::unique_ptr<mape> kitchen_mape_; /**< the kitchen mape. */
    /* adaptivity related member variables */
    std::unordered_map<swap_key, swap_state, tuple_hash> pending_swaps_;
 
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
     */
    void
    handle_robot_registration(std::string _endpoint, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities);

    /**
     * @brief Checks if the position is involved in a position swap and returns the corresponding map key.
     * 
     * @param _position the requested position to check for.
     * @param _out_key stores the corresponding key for the pending_swaps_ map.
     * @return true if position is swapping.
     * @return false if position is not swapping.
     */
    bool
    is_robot_position_swapping(position_t _position, swap_key& _out_key);

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
     * @param _endpoint the requester's endpoint.
     * @param _type the requester's type.
     */
    void
    handle_next_robot_request(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_String _endpoint, UA_String _type);

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
     * @brief Instructs a remote robot to swap its position with another robot.
     * 
     * @param _from the robot's current position.
     * @param _to the robot's new target position.
     */
    void
    swap_robot_positions(position_t _from, position_t _to);

    /**
     * @brief Extracts return values of adaptation call.
     * 
     * @param _output_size the count of returned output values.
     * @param _output the variant containing the output values.
     * @return true if the robot will adapt.
     * @return false if the robot denies the adaptation request.
     */
    bool
    adaptivity_action_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Called when robot switched to its new position.
     * 
     * @param _old_position the robot's old position.
     * @param _new_position the robot's new position.
     */
    void
    position_swapped_callback(position_t _old_position, position_t _new_position);

    /**
     * @brief Erases stale pending entries where both positions are not occupied anymore.
     * 
     */
    void
    erase_stale_pending_swap_entries();

    /**
     * @brief Instructs a remote robot to reconfigure its capability profile.
     * 
     * @param _robot_position the position of the robot.
     * @param _new_capabilities_profile the new capabilities profile.
     */
    void
    reconfigure_robot_capability(position_t _robot_position, std::string _new_capabilities_profile);

    /**
     * @brief Called when robot reconfigured its capabilities.
     * 
     * @param _robot_position the robot position.
     */
    void
    capabilities_reconfigured_callback(position_t _robot_position);

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
    controller(std::unique_ptr<mape> _kitchen_mape);

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