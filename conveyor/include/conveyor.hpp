/**
 * @file conveyor.hpp
 * @brief OPC UA Conveyor agent that coordinates dish handover between robots and the kitchen.
 *
 * @details
 * The conveyor hosts an OPC UA server (open62541) that models a circular belt with one plate per
 * robot plus an output position. It receives notifications from robots about finished or partially
 * finished dishes, retrieves dishes, schedules belt movement, delivers completed dishes to the
 * kitchen, and requests the next suitable robot from the controller for partially finished dishes.
 *
 * The implementation is multithreaded: The conveyor hosts its own server iterate loop and maintains
 * client connections to external services.
 */
#ifndef CONVEYOR_HPP
#define CONVEYOR_HPP

#define PLATE_INSTANCE_NAME "KitchenPlate"
#define OUTPUT_POSITION 0

#include <open62541/server.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <unistd.h>
#include "method_node_caller.hpp"
#include "client_connection_establisher.hpp"
#include "types.hpp"
#include "browsenames.h"
#include "node_value_subscriber.hpp"
#include "robot_tool.hpp"
#include "object_type_node_inserter.hpp"
#include "node_browser_helper.hpp"
#include "discovery_util.hpp"

using namespace cps_kitchen;

typedef std::function<void(position_t)> mark_robot_for_removal_callback_t; /**< the callback declaration to mark robots for removal. */
typedef std::function<void(position_t, position_t)> position_swapped_callback_t; /**< the callback declaration to notify about position change. */

/**
 * @brief Remote robot client to pass and retrieve dishes to/from kitchen robots and maintaining the connectivity.
 * 
 */
struct remote_robot {
    private:
        UA_Client* client_; /**< the OPC UA remote robot client pointer. */
        std::string endpoint_; /**< the endpoint address. */
        std::atomic<position_t> position_; /**< the position on the conveyor belt. */
        mark_robot_for_removal_callback_t mark_robot_for_removal_callback_; /**< the callback to mark robots for removal. */
        position_swapped_callback_t position_swapped_callback_; /**< the callback to notify about position change. */
        std::unordered_map<std::string, object_method_info> method_id_map_; /**< the map holding the node ids of client methods. */
        std::unordered_map<std::string, UA_NodeId> attribute_id_map_; /**< the map holding the ids of remote robot attributes. */
        std::atomic<bool> running_; /**< flag to indicate whether the client thread should run. */
        std::thread client_iterate_thread_; /**< the client iteration thread. */
        std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */
        bool initial_subscription_; /**< flag to indicate initial subscription notification. */

    public:
        /**
         * @brief Constructs a new remote robot object.
         * 
         * @param _endpoint the remote robot's endpoint.
         * @param _position the position of the remote robot.
         * @param _mark_robot_for_removal_callback the mark for removal callback.
         * @param _position_swapped_callback the position swapped callback.
         */
        remote_robot(std::string _endpoint, position_t _position,
                    mark_robot_for_removal_callback_t _mark_robot_for_removal_callback, position_swapped_callback_t _position_swapped_callback) :
                    endpoint_(_endpoint), position_(_position), client_(nullptr), running_(true),
                    mark_robot_for_removal_callback_(_mark_robot_for_removal_callback),
                    position_swapped_callback_(_position_swapped_callback), initial_subscription_(true) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
            client_connection_establisher robot_client_connection_establisher;
            bool connected = robot_client_connection_establisher.establish_connection(client_, endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error establishing robot client session");
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            /* Get the position attribute id. */
            attribute_id_map_[POSITION] = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, POSITION);
            if (UA_NodeId_equal(&attribute_id_map_[POSITION], &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            /* Subscribe to position changes. */
            node_value_subscriber nv_subscriber;
            UA_StatusCode status = nv_subscriber.subscribe_node_value(client_, attribute_id_map_[POSITION], position_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s", __FUNCTION__, POSITION);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            if ((method_id_map_[HANDOVER_FINISHED_ORDER] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, HANDOVER_FINISHED_ORDER)) == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, HANDOVER_FINISHED_ORDER);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            if ((method_id_map_[RECEIVE_TASK] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, RECEIVE_TASK)) == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECEIVE_TASK);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
            try {
                client_iterate_thread_ = std::thread([this]() {
                    while(running_.load()) {
                        {
                            std::lock_guard<std::mutex> lock(client_mutex_);
                            UA_StatusCode status = UA_Client_run_iterate(client_, 1);
                            if (status != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot client at position %d (%s)", __FUNCTION__, position_, UA_StatusCode_name(status));
                                running_.store(false);
                                mark_robot_for_removal_callback_(position_.load());
                                return;
                            }
                        }
                        if (usleep(1*1000)) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at robot client iterate sleep", __FUNCTION__);
                            running_.store(false);
                            mark_robot_for_removal_callback_(position_.load());
                            return;
                        }
                        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Starting the next client iterate", __FUNCTION__);
                    }
                });
            } catch (...) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running the robot client iterate thread at position %d", __FUNCTION__, position_);
                running_.store(false);
                mark_robot_for_removal_callback_(position_.load());
                return;
            }
        }

        /**
         * @brief Destroys the remote robot object.
         * 
         */
        ~remote_robot() {
            running_.store(false);
            if (client_iterate_thread_.joinable())
                client_iterate_thread_.join();
            UA_Client_delete(client_);
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
         * @brief Returns the robot's position at the conveyor.
         * 
         * @return position_t the robot's position at the conveyor.
         */
        position_t get_position() const {
            return position_.load();
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
            self->position_.store(*(position_t*)_value->value.data);
            if (self->initial_subscription_) {
                self->initial_subscription_ = false;
                return;
            }
            self->position_swapped_callback_(old_position, self->position_.load());
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's position updated/changed to %d ", __FUNCTION__, self->position_);
        }

        /**
         * @brief Notifies the robot to hand over the finished order.
         * 
         * @param _output_size the count of returned output values.
         * @param _output the variant containing the output values.
         * 
         * @return UA_StatusCode the status whether the method call was successful.
         */
        UA_StatusCode handover_finished_order(size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "HANDOVER: Retrieve finished order from robot on position %d", position_.load());
            method_node_caller handover_finished_order_caller;
            object_method_info omi = method_id_map_[HANDOVER_FINISHED_ORDER];
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                status = handover_finished_order_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling %s method (%s)", __FUNCTION__, HANDOVER_FINISHED_ORDER, UA_StatusCode_name(status));
                    running_.store(false);
                    mark_robot_for_removal_callback_(position_.load());
                    return UA_STATUSCODE_BAD;
                }
            }
            return status;
        }

        /**
         * @brief Instructs the remote robot to process a partially processed dish.
         * 
         * @param _recipe_id the recipe ID of the dish.
         * @param _processed_steps the processed steps of the recipe ID so far.
         * @param _output_size the count of returned output values.
         * @param _output the variant containing the output values.
         * 
         * @return UA_StatusCode the status whether the method call was successful.
         */
        UA_StatusCode instruct(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "INSTRUCTIONS: Instruct robot on position %d to cook recipe %d after step %d", position_, _recipe_id, _processed_steps);
            method_node_caller receive_robot_task_caller;
            receive_robot_task_caller.add_scalar_input_argument(&_recipe_id, UA_TYPES_UINT32);
            receive_robot_task_caller.add_scalar_input_argument(&_processed_steps, UA_TYPES_UINT32);
            object_method_info omi = method_id_map_[RECEIVE_TASK];
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                status = receive_robot_task_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling %s method (%s)", __FUNCTION__, RECEIVE_TASK, UA_StatusCode_name(status));
                    running_.store(false);
                    mark_robot_for_removal_callback_(position_.load());
                    return UA_STATUSCODE_BAD;
                }
            }
            return status;
        }
};

/**
 * @brief Wrapper for representing plates on the conveyor and tracking their status and occupancy status.
 * 
 */
struct plate {
    private:
        const plate_id_t id_; /**< the plate id. */
        position_t position_; /**< the current position on the conveyor. */
        recipe_id_t placed_recipe_id_; /**< the recipe currently covering the plate. */
        UA_UInt32 processed_steps_of_placed_recipe_id_; /**< the processed steps of the current dish. */
        UA_Boolean occupied_; /**< indicates whether the plate is occupied or free. */
        UA_Boolean is_dish_finished_; /**< indicates whether it holds a completed dish or a partially finished dish when occupied. */
        position_t target_position_; /**< the target position for the next preparation steps or the output when finished. */
        std::string instance_name_id_; /**< the instance name id in the address space. */
        object_type_node_inserter& plate_type_inserter_; /**< the plate type inserter for adding the plate's attributes to the address space. */
    public:
        /**
         * @brief Setup the plate object type.
         * 
         * @param _plate_type_inserter the plate type inserter.
         * @param _conveyor the conveyor server.
         * @return UA_StatusCode the status code.
         */
        static UA_StatusCode setup_plate_object_type(object_type_node_inserter& _plate_type_inserter, UA_Server* _conveyor) {
            UA_StatusCode status;
            /* Add attributes. */
            status = _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_ID);
            status |= _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_POSITION);
            status |= _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_RECIPE_ID);
            status |= _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_OCCUPIED);
            /* Add plate type constructor. */
            status |= _plate_type_inserter.add_object_type_constructor(_conveyor, _plate_type_inserter.get_object_type_id(PLATE_TYPE));
            return status;
        }

        /**
         * @brief Constructs a new plate object.
         * 
         * @param _id the plate id.
         * @param _position the plate position.
         * @param _conveyor_instance_id the conveyor instance id.
         * @param _plate_type_inserter the plate type inserter.
         */
        plate(plate_id_t _id, position_t _position, UA_NodeId _conveyor_instance_id, object_type_node_inserter& _plate_type_inserter) : id_(_id), position_(_position), placed_recipe_id_(0),
                processed_steps_of_placed_recipe_id_(0), occupied_(false), is_dish_finished_(false), target_position_(0), instance_name_id_(std::string(PLATE_INSTANCE_NAME) + " " + std::to_string(id_)), plate_type_inserter_(_plate_type_inserter) {
            /* Instantiate plate type. */
            UA_StatusCode status = plate_type_inserter_.add_object_instance(instance_name_id_.c_str(), PLATE_TYPE, _conveyor_instance_id, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding plate object instance (%s)", __FUNCTION__, UA_StatusCode_name(status));
                return;
            }
            /* Set attribute values. */
            status = plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_ID, const_cast<plate_id_t*>(&id_), UA_TYPES_UINT32);
            status |= plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_POSITION, &position_, UA_TYPES_UINT32);
            status |= plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_RECIPE_ID, &placed_recipe_id_, UA_TYPES_UINT32);
            status |= plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_OCCUPIED, &occupied_, UA_TYPES_BOOLEAN);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting plate attributes (%s)", __FUNCTION__, UA_StatusCode_name(status));
            }
        }

        /**
         * @brief Destroys the plate object.
         * 
         */
        ~plate() {
        }

        /**
         * @brief Constructs a new plate object from another plate.
         * 
         * @param _plate the plate.
         */
        plate(const plate& _plate) : id_(_plate.id_), position_(_plate.position_), placed_recipe_id_(_plate.placed_recipe_id_), processed_steps_of_placed_recipe_id_(_plate.processed_steps_of_placed_recipe_id_),
            occupied_(_plate.occupied_), is_dish_finished_(_plate.is_dish_finished_), target_position_(_plate.target_position_), instance_name_id_(_plate.instance_name_id_), plate_type_inserter_(_plate.plate_type_inserter_) {
        }

        /**
         * @brief Returns the plate id.
         * 
         * @return plate_id_t the plate id.
         */
        plate_id_t get_plate_id() const {
            return id_;
        }

        /**
         * @brief Set the position and write to the information node.
         * 
         * @param _position the plate position.
         */
        void set_position(position_t _position) {
            position_ = _position;
            plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_POSITION, &position_, UA_TYPES_UINT32);
        }

        /**
         * @brief Get the current position of the plate on the conveyor.
         * 
         * @return position_t the current position on the conveyor.
         */
        position_t get_position() const {
            return position_;
        }

        /**
         * @brief Places the finished dish on the plate.
         * 
         * @param _placed_recipe_id the placed dish's recipe id.
         */
        void place_recipe_id(recipe_id_t _placed_recipe_id) {
            placed_recipe_id_ = _placed_recipe_id;
            plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_RECIPE_ID, &placed_recipe_id_, UA_TYPES_UINT32);
        }

        /**
         * @brief Get the placed recipe id.
         * 
         * @return recipe_id_t the recipe id.
         */
        recipe_id_t get_placed_recipe_id() const {
            return placed_recipe_id_;
        }

        /**
         * @brief Sets the target position.
         * 
         * @param _target_position the target position.
         */
        void set_target_position(position_t _target_position) {
            target_position_ = _target_position;
        }

        /**
         * @brief Get the target position to where the dish has to be transferred.
         * 
         * @return position_ the target position.
         */
        position_t get_target_position() const {
            return target_position_;
        }

        /**
         * @brief Sets the processed steps of the currently placed recipe id on the plate.
         * 
         * @param _processed_steps_of_placed_recipe_id the processed steps.
         */
        void set_processed_steps(UA_UInt32 _processed_steps_of_placed_recipe_id) {
            processed_steps_of_placed_recipe_id_ = _processed_steps_of_placed_recipe_id;
        }

        /**
         * @brief Returns the processed steps of the currently placed recipe id on the plate.
         * 
         * @return UA_UInt32 the processed steps.
         */
        UA_UInt32 get_processed_steps() const {
            return processed_steps_of_placed_recipe_id_;
        }

        /**
         * @brief Sets the plate's occupied state.
         * 
         * @param _occupied the occupied state.
         */
        void set_occupied(UA_Boolean _occupied) {
            occupied_ = _occupied;
            plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_OCCUPIED, &occupied_, UA_TYPES_BOOLEAN);
        }

        /**
         * @brief Returns whether plate is occupied or not.
         * 
         * @return UA_Boolean indicates whether plate is occupied or not.
         */
        UA_Boolean is_occupied() const {
            return occupied_;
        }

        /**
         * @brief Sets the dish finished state.
         * 
         * @param _is_dish_finished the dish finished state.
         */
        void set_dish_finished(UA_Boolean _is_dish_finished) {
            is_dish_finished_ = _is_dish_finished;
        }

        /**
         * @brief Returns whether the placed dish is finished or not.
         * 
         * @return UA_Boolean indicates whether the dish is finished (deliver to output) or needs to be processed further by another robot.
         */
        UA_Boolean is_dish_finished() const {
            return is_dish_finished_;
        }
};

class conveyor {

/**
 * @brief The states in which the conveyor can be.
 * 
 */
enum state {
    IDLING,
    MOVING
};

private:
    /* conveyor related member variables. */
    UA_Server* server_; /**< the OPC UA conveyor server pointer. */
    object_type_node_inserter conveyor_type_inserter_; /**< the conveyor type inserter for adding the conveyor's attributes and methods to the address space. */
    object_type_node_inserter plate_type_inserter_; /**< the plate type inserter for adding the plate's attributes to the address space. */
    std::atomic<bool> running_; /**< flag to indicate whether the server and client threads should run. */
    state state_status_; /**< the current state of the conveyor. */
    std::vector<plate> plates_; /**< the plates on the conveyor. */
    std::thread server_iterate_thread_; /**< the server iteration thread. */
    discovery_util discovery_util_; /**< the discovery utility. */
    std::unordered_set<plate_id_t> occupied_plates_; /**< the currently occupied plates. */
    std::unordered_map<position_t, plate_id_t> position_plate_id_map_; /**< the map tracking the current positions of the plates. */
    std::unordered_map<position_t, std::string> notifications_map_; /**< the notifications received by the robots. */
    std::unordered_map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_; /**< the map tracking the current positions of robots. */
    std::unordered_set<position_t> robots_to_be_removed_; /**< the set holding robots to be removed. */
    std::unordered_map<std::string, object_method_info> method_id_map_; /**< the map holding the node ids of client methods. */
    /* controller related member variables. */
    std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */
    std::thread client_iterate_thread_; /**< the client iteration thread. */
    UA_Client* controller_client_; /**< the OPC UA controller client pointer. */
    /* robot related member variables. */
    std::mutex mark_for_removal_mutex_; /**< the mark for removal mutex for synchronizing the to be removed set. */
    std::mutex position_remote_robot_map_mutex_; /**< the position_remote_robot_map mutex for synchronizing map access. */
    /* kitchen related member variables. */
    UA_Client* kitchen_client_; /**< the OPC UA kitchen client pointer. */

    /**
     * @brief Extracts the remote robot port and position on which a finished order is ready to be retrieved.
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
    receive_finished_order_notification(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);

    /**
     * @brief Stores the extracted finished order notification parameters from the receive_finished_order_notification method
     * and schedules the retrieval of finished orders if the conveyor is idling.
     * 
     * @param _robot_endpoint the endpoint from which the finished order notification is sent.
     * @param _robot_position the position on which the finished order is ready to be retrieved.
     * @param _output the output pointer to store return parameters.
     */
    void
    handle_finished_order_notification(std::string _robot_endpoint, position_t _robot_position, UA_Variant* _output);

    /**
     * @brief Timed callback to call handle_retrieve_finished_orders.
     * 
     * @param _server the server instance from which this method is called.
     * @param _data the conveyor instance passed to the scheduling call.
     */
    static void
    retrieve_finished_orders(UA_Server* _server, void* _data);

    /**
     * @brief Retrieves finished dishes if possible or keeps moving if there occupied plates.
     * 
     */
    void
    handle_retrieve_finished_orders();

    /**
     * @brief Moves the conveyor and updates plate position accordingly.
     * 
     * @param _steps the steps the conveyor has to move.
     */
    void
    move_conveyor(steps_t _steps);

    /**
     * @brief Delivers the finished order on the output position.
     * 
     */
    void
    deliver_finished_order();

    /**
     * @brief Extracts the returned result to indicate whether the completed dish is delivered successfully.
     * 
     * @param _output_size the count of returned output values.
     * @param _output the variant containing the output values.
     * @return UA_StatusCode the status code indicating the successful or failed delivery.
     */
    UA_StatusCode
    receive_completed_order_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Idles if there are no notifications and occupied plates. Otherwise it processes the notifications and occupied plates.
     * 
     */
    void
    determine_next_movement();

    /**
     * @brief Extracts the robot port and position as well as the recipe id of the finished dish.
     * 
     * @param _output_size the count of returned output values.
     * @param _output the variant containing the output values.
     */
    void
    handover_finished_order_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Retrieves finished orders if corresponding plate is not occupied and schedules the next movement.
     * 
     * @param _remote_robot_endpoint the endpoint from which the finished dish is retrieved.
     * @param _remote_robot_position the position on which the finished dish is retrieved.
     * @param _finished_recipe the recipe id of the finished dish.
     * @param _processed_steps the steps count processed so far.
     * @param _is_dish_finished indicates if the dish is finished partially or completely.
     */
    void
    handle_handover_finished_order(std::string _remote_robot_endpoint, position_t _remote_robot_position, recipe_id_t _finished_recipe, UA_UInt32 _processed_steps, UA_Boolean _is_dish_finished);

    /**
     * @brief Requests next robot.
     * 
     */
    void
    request_next_robot(plate& _plate);

    /**
     * @brief Timed callback to call move_conveyor, deliver_finished_order and determine_next_movement.
     * 
     * @param _server the server instance from which this method is called.
     * @param _data the conveyor instance passed to the scheduling call.
     */
    static void
    perform_movement(UA_Server* _server, void* _data);

    /**
     * @brief Extracts the returned robot state parameters and updates plates.
     * 
     * @param _output_size the count of returned output values.
     * @param _output the variant containing the output values.
     * @return true if delivery succeedes.
     * @return false if delivery fails.
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
     * @brief Marks a remote robot for removal.
     * 
     * @param _position the position of the remote robot to mark for removal.
     */
    void
    mark_robot_for_removal(position_t _position); 

    /**
     * @brief Removes all marked robots from the conveyor.
     * 
     */
    void
    remove_marked_robots();

    /**
     * @brief Resets the plate attributes to their default values.
     * 
     * @param _plate the plate.
     */
    void
    reset_plate(plate& _plate);

    /**
     * @brief Joins all started threads.
     * 
     */
    void
    join_threads();

public:
    /**
     * @brief Construct a new conveyor object.
     * 
     * @param _robot_count the robot count.
     */
    conveyor(UA_UInt32 _robot_count);

    /**
     * @brief Destroy the conveyor object.
     * 
     */
    ~conveyor();

    /**
     * @brief Checks if initialization was successful and joins all started threads.
     * 
     */
    void
    start();

    /**
     * @brief Stops the conveyor and shuts it down.
     * 
     */
    void
    stop();
};

#endif // CONVEYOR_HPP