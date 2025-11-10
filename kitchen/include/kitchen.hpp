/**
 * @file kitchen.hpp
 * @brief OPC UA CPS Kitchen server assigning robots with placed orders and monitoring
 * the connectivity status of all robot, controller and conveyor agents.
 *
 * @details
 * This header declares the CPS kitchen agent which exposes an OPC UA server, registers
 * itself to a discovery server, communicates with the controller and kitchen robots via
 * OPC UA method calls, and assigns orders to kitchen robots as well as receives completed
 * dishes from the conveyor. Additionally, it monitors the connectivity status of kitchen
 * robots, the controller and the conveyor.
 *
 * The implementation is multithreaded: the kitchen hosts its own server iterate loop,
 * runs a worker to assign placed orders and maintatins client connections to external services.
 */
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
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/unordered_set.hpp>
#include <boost/functional/hash.hpp>

#include "object_type_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "node_browser_helper.hpp"
#include "discovery_util.hpp"
#include "browsenames.h"
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "types.hpp"
#include "recipe_parser.hpp"
#include "robot_state.hpp"
#include "information_node_reader.hpp"

using namespace cps_kitchen;

typedef std::function<void(position_t, position_t)> position_swapped_callback_t; /**< the callback declaration to notify about position change. */

struct remote_robot {
    private:
        UA_Client* client_; /**< the OPC UA remote robot client pointer. */
        std::string endpoint_; /**< the remote robot's endpoint address. */
        std::atomic<position_t> cached_position_; /**< the remote robot's position on the conveyor belt. */
        std::atomic<bool> running_; /**< flag to indicate whether the client thread should run. */
        object_type_node_inserter& remote_robot_type_inserter_; /**< the remote robot type inserter for adding the remote robot's attributes to the address space. */
        position_swapped_callback_t position_swapped_callback_; /**< the callback to notify about position change. */
        std::unique_ptr<node_value_subscriber> nv_subscriber_; /**< the node value subscriber. */
        std::thread client_iterate_thread_; /**< the client iteration thread. */
        std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */
        std::unordered_map<std::string, object_method_info> method_id_map_; /**< the map holding the ids of remote robot methods. */
        std::unordered_map<std::string, UA_NodeId> attribute_id_map_; /**< the map holding the ids of remote robot attributes. */
        bool initial_subscription_; /**< flag to indicate initial subscription notification. */
    public:
        /**
         * @brief Sets up the remote robot object type.
         * 
         * @param _remote_robot_type_inserter the remote robot type inserter.
         * @param _kitchen the kitchen server.
         * @return UA_StatusCode the status code.
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
         * @brief Constructs a new remote robot object.
         * 
         * @param _endpoint the remote robot's endpoint url.
         * @param _position the remote robot's position.
         * @param _remote_robot_type_inserter the remote robot type inserter.
         * @param _mark_robot_for_removal_callback the mark robot for removal callback.
         * @param _position_swapped_callback the position swapped callback.
         */
        remote_robot(std::string _endpoint, UA_UInt32 _position, object_type_node_inserter& _remote_robot_type_inserter,
                    position_swapped_callback_t _position_swapped_callback) :
                    client_(nullptr), endpoint_(_endpoint), cached_position_(_position), running_(true),
                    remote_robot_type_inserter_(_remote_robot_type_inserter),
                    position_swapped_callback_(_position_swapped_callback), initial_subscription_(true) {
        }

        /**
         * @brief Initializes and starts this remote robot.
         * 
         * @return UA_StatusCode the status code.
         */
        UA_StatusCode
        initialize_and_start() {
            if (client_ != nullptr) {
                return running_.load() ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BAD;
            }
            client_connection_establisher robot_client_connection_establisher;
            bool connected = robot_client_connection_establisher.establish_connection(client_, endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error establishing robot client session", __FUNCTION__);
                return UA_STATUSCODE_BAD;
            }
            /* Get the position attribute id. */
            attribute_id_map_[POSITION] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, POSITION);
            if (UA_NodeId_equal(&attribute_id_map_[POSITION], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
                return UA_STATUSCODE_BAD;
            }
            /* Get the availablility attribute id. */
            attribute_id_map_[AVAILABILITY] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, AVAILABILITY);
            if (UA_NodeId_equal(&attribute_id_map_[AVAILABILITY], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, AVAILABILITY);
                return UA_STATUSCODE_BAD;
            }
            /* Subscribe to position changes. */
            nv_subscriber_ = std::make_unique<node_value_subscriber>(client_);
            UA_StatusCode status = nv_subscriber_->subscribe_node_value(attribute_id_map_[POSITION], position_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s", __FUNCTION__, POSITION);
                return UA_STATUSCODE_BAD;
            }
            /* Set connectvitiy. */
            status = remote_robot_type_inserter_.set_scalar_attribute(remote_robot_instance_name(cached_position_.load()), CONNECTIVITY, &connected, UA_TYPES_BOOLEAN);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting remote robot connectivity attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
                return UA_STATUSCODE_BAD;
            }
            /* Get receive task method id. */
            if ((method_id_map_[RECEIVE_TASK] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, RECEIVE_TASK)) == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECEIVE_TASK);
                return UA_STATUSCODE_BAD;
            }
            try {
                client_iterate_thread_ = std::thread([this]() {
                    while(running_.load()) {
                        {
                            std::lock_guard<std::mutex> lock(client_mutex_);
                            UA_StatusCode status = UA_Client_run_iterate(client_, 1);
                            if (status != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot client at position %d (%s)", __FUNCTION__, cached_position_.load(), UA_StatusCode_name(status));
                                running_.store(false);
                                return UA_STATUSCODE_BAD;
                            }
                        }
                        if (usleep(1*1000)) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at robot client iterate sleep", __FUNCTION__);
                            running_.store(false);
                            return UA_STATUSCODE_BAD;
                        }
                        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Starting the next client iterate", __FUNCTION__);
                    }
                    return UA_STATUSCODE_BAD;
                });
            } catch (...) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running the robot client iterate thread at position %d", __FUNCTION__, cached_position_.load());
                running_.store(false);
                return UA_STATUSCODE_BAD;
            }
            return UA_STATUSCODE_GOOD;
        }

        /**
         * @brief Instructs the remote robot to process a dish.
         * 
         * @param _recipe_id the recipe ID of the dish.
         * @param _processed_steps the processed steps of the recipe ID so far.
         * @param _addressed_position the addressed position.
         * @param _output_size the count of returned output values.
         * @param _output the variant containing the output values.
         * 
         * @return UA_StatusCode the status whether the method call was successful.
         */
        UA_StatusCode
        instruct(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, position_t _addressed_position, size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "INSTRUCTIONS: Instruct robot on position %d to cook recipe %d from step %d", cached_position_.load(), _recipe_id, _processed_steps);
            method_node_caller receive_robot_task_caller;
            receive_robot_task_caller.add_scalar_input_argument(&_recipe_id, UA_TYPES_UINT32);
            receive_robot_task_caller.add_scalar_input_argument(&_processed_steps, UA_TYPES_UINT32);
            receive_robot_task_caller.add_scalar_input_argument(&_addressed_position, UA_TYPES_UINT32);
            object_method_info omi = method_id_map_[RECEIVE_TASK];
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                status = receive_robot_task_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling instruct method (%s)", __FUNCTION__, UA_StatusCode_name(status));
                    running_.store(false);
                    return UA_STATUSCODE_BAD;
                }
            }
            return status;
        }

        /**
         * @brief Returns the robot's endpoint.
         * 
         * @return std::string the robot's endpoint url.
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
        get_position() {
            std::lock_guard<std::mutex> lock(client_mutex_);
            information_node_reader inr;
            if (inr.read_information_node(client_, attribute_id_map_[POSITION]) != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not read the %s attribute id", __FUNCTION__, POSITION);
                running_.store(false);
                return 0;
            }
            return *(position_t*)inr.get_variant()->data;
        }

        bool
        is_available() {
            std::lock_guard<std::mutex> lock(client_mutex_);
            information_node_reader inr;
            if (inr.read_information_node(client_, attribute_id_map_[AVAILABILITY]) != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not read the %s attribute id", __FUNCTION__, AVAILABILITY);
                running_.store(false);
                return false;
            }
            return *(UA_Boolean*)inr.get_variant()->data;
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
                self->running_.store(false);
                return;
            }
            UA_UInt32 old_position = self->cached_position_.load();
            self->cached_position_.store(*(position_t*)_value->value.data);
            if (self->initial_subscription_) {
                self->initial_subscription_ = false;
                return;
            }
            self->position_swapped_callback_(old_position, self->cached_position_.load());
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's position updated/changed to %d ", __FUNCTION__, self->cached_position_);
        }

        /**
         * @brief Helper method for self defined remote robot instance names according to their position.
         * 
         * @param _position the remote robot's position.
         * @return std::string the remote robot's instance name string.
         */
        static std::string remote_robot_instance_name(position_t _position) {
            return REMOTE_ROBOT_INSTANCE_NAME_PREFIX + std::to_string(_position);
        }

        /**
         * @brief Indicates whether the robot is stopped and not running anymore.
         * 
         * @return true if robot is stopped.
         * @return false if robot is still running.
         */
        bool
        is_stopped() {
            return !running_.load();
        }

        /**
         * @brief Destroys the remote robot object.
         * 
         */
        ~remote_robot() {
            running_.store(false);
            if (client_iterate_thread_.joinable())
                client_iterate_thread_.join();
            nv_subscriber_.reset();
            UA_Client_delete(client_);
            UA_Boolean connectivity = false;
            remote_robot_type_inserter_.set_scalar_attribute(remote_robot_instance_name(cached_position_.load()), CONNECTIVITY, &connectivity, UA_TYPES_BOOLEAN);
        }
};

class kitchen {
private:
    /* kitchen related member variables. */
    UA_Server* server_; /**< the OPC UA kitchen server. */
    std::string kitchen_uri_; /**< the kitchen's uniform resource identifier. */
    UA_String server_endpoint_; /**< the kitchen's endpoint address. */
    UA_String type_; /**< the kitchen's agent type. */
    object_type_node_inserter kitchen_type_inserter_; /**< the kitchen type inserter for adding the kitchen's attributes and methods to the address space. */
    std::atomic<bool> running_; /**< flag to indicate whether the server and client threads should run. */
    discovery_util discovery_util_; /**< the discovery utility. */
    std::unordered_map<std::string, object_method_info> method_id_map_; /**< the map holding the node ids of client methods. */
    std::thread server_iterate_thread_; /**< the server iteration thread. */
    std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */
    std::thread client_iterate_thread_; /**< the client iteration thread. */
    std::thread worker_thread_; /**< the worker thread for assigning placed orders to remote robots. */
    boost::asio::io_context io_context_; /**< the io context managing the worker thread. */
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type, void, void> work_guard_; /**< the work guard for the io_context_. */
    boost::asio::steady_timer placing_timer_; /**< the placing timer. */
    bool placing_gate_open_; /**< the placing gate. */
    std::queue<std::function<void()>> placing_queue_ /**< the placing queue. */;
    /* remote robot related member variables. */
    std::thread cyclic_remote_robot_discovery_thread_; /**< the thread updating the connectivity status of remote robots in the address space. */
    std::unordered_map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_; /**< the map holding the remote robot instances. */
    object_type_node_inserter remote_robot_type_inserter_; /**< the remote robot type inserter for adding the robot's attributes to the address space. */
    uint32_t robot_count_; /**< the total robot count in the kitchen. */
    /* controller related member variables. */
    UA_Client* controller_client_; /**< the OPC UA controller client pointer. */
    object_type_node_inserter remote_controller_type_inserter_; /**< the remote controller type inserter for adding the controller's attributes to the address space. */
    std::condition_variable remote_controller_connected_cv_; /**< the condition variable to wait for the controller connection to be restored. */
    /* conveyor related member variables. */
    UA_Client* conveyor_client_; /**< the OPC UA conveyor client pointer. */
    object_type_node_inserter remote_conveyor_type_inserter_; /**< the remote conveyor type inserter for adding the conveyor's attributes to the address space. */
    /* recipe related member variables. */
    recipe_parser recipe_parser_; /**< the recipe parser. */
    /* random distribution. */
    std::random_device random_device_; /**< the random number generator device. */
    std::mt19937 mersenne_twister_; /**< the mersenne twister for uniform pseudo-random number generation. */
    std::uniform_int_distribution<std::uint32_t> uniform_int_distribution_; /**< uniform discrete distribution for random numbers. */

    /**
     * @brief Receives a completed order.
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
    receive_completed_order(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Places a random order.
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
    place_random_order(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Arms the placing gate.
     * 
     */
    void
    arm_placing_gate();

    /**
     * @brief Handles the random order request.
     * 
     */
    void
    handle_random_order_request();

    /**
     * @brief Extracts the returned remote robot parameters.
     * 
     * @param _output_size the count of returned output values.
     * @param _output the variant containing the output values.
     * @return true if call was successful.
     * @return false if call failed.
     */
    bool
    choose_next_robot_called(size_t _output_size, UA_Variant *_output);

    /**
     * @brief Receives the next suitable robot for a requested recipe.
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
    receive_next_robot(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Handles the received next robot response.
     * 
     * @param _robot_position the robot position.
     * @param _robot_endpoint the robot endpoint.
     * @param _recipe_id the recipe id.
     */
    void
    handle_receive_next_robot(position_t _robot_position, std::string _robot_endpoint, recipe_id_t _recipe_id);

    /**
     * @brief Extracts the returned robot state parameters.
     * 
     * @param _output_size the count of returned output values.
     * @param _output the variant containing the output values.
     * @return true if instruction suceeded.
     * @return false if instruction failed.
     */
    bool
    receive_robot_task_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Called when robot switched to its new position.
     * 
     * @param _old_position the robot's old position.
     * @param _new_position the robot's new position.
     */
    void
    position_swapped_callback(position_t _old_position, position_t _new_position);

    /**
     * @brief Helper method for incrementing X_ORDERS attribute nodes.
     * 
     * @param _attribute_name the attribute name.
     * @return UA_StatusCode the status code indicating whether incrementing succeeded.
     */
    UA_StatusCode
    increment_orders_counter(std::string _attribute_name);

    /**
     * @brief Removes all stopped robots from the kitchen.
     * 
     */
    void
    remove_stopped_robots();

    /**
     * @brief Joins all started threads.
     * 
     */
    void
    join_threads();

public:
    /**
     * @brief Constructs a new kitchen object
     * 
     * @param _robot_count the total robot count in the kitchen.
     */
    kitchen(uint32_t _robot_count);

    /**
     * @brief Destroys the kitchen object.
     * 
     */
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