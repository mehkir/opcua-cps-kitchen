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
#include <set>
#include <unordered_map>
#include <memory>
#include <condition_variable>
#include "method_node_caller.hpp"
#include "client_connection_establisher.hpp"
#include "types.hpp"
#include "browsenames.h"
#include "node_value_subscriber.hpp"
#include "robot_tool.hpp"
#include "object_type_node_inserter.hpp"
#include "node_browser_helper.hpp"

using namespace cps_kitchen;

struct remote_robot {
    private:
        UA_Client* client_;
        const port_t port_;
        const position_t position_;
        std::unordered_map<std::string, object_method_info> method_id_map_;

    public:
        /**
         * @brief Construct a new remote robot object.
         * 
         * @param _port the port of the remote robot
         * @param _position the position of the remote robot
         */
        remote_robot(port_t _port, position_t _position) :  port_(_port), position_(_position), client_(UA_Client_new()) {
            client_connection_establisher robot_client_connection_establisher(client_);
            bool connected = robot_client_connection_establisher.establish_connection("opc.tcp://localhost:" + std::to_string(port_));
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error establishing robot client session");
                return;
            }
            method_id_map_[HANDOVER_FINISHED_ORDER] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, HANDOVER_FINISHED_ORDER);
            method_id_map_[RECEIVE_TASK] = node_browser_helper().get_method_id(client_, ROBOT_TYPE, RECEIVE_TASK);
        }

        /**
         * @brief Destroy the remote robot object.
         * 
         */
        ~remote_robot() {
            UA_Client_delete(client_);
        }

        /**
         * @brief Returns the robot's port
         * 
         * @return port_t the robot's port
         */
        port_t
        get_port() const {
            return port_;
        }

        /**
         * @brief Returns the robot's position at the conveyor
         * 
         * @return position_t the robot's position at the conveyor
         */
        position_t get_position() const {
            return position_;
        }

        /**
         * @brief Notifies the robot to hand over the finished order
         * 
         * @param _output_size the count of returned output values
         * @param _output the variant containing the output values
         * 
         * @return UA_StatusCode the status whether the method call was successful
         */
        UA_StatusCode handover_finished_order(size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "HANDOVER: Retrieve finished order from robot on position %d with port %d", position_, port_);
            method_node_caller handover_finished_order_caller;
            object_method_info omi = method_id_map_[HANDOVER_FINISHED_ORDER];
            if (omi == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, HANDOVER_FINISHED_ORDER);
                return UA_STATUSCODE_BAD;
            }
            UA_StatusCode status = handover_finished_order_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
            }
            return status;
        }

        /**
         * @brief Instructs the remote robot to process a partially processed dish.
         * 
         * @param _recipe_id the recipe ID of the dish
         * @param _processed_steps the processed steps of the recipe ID so far
         * @param _output_size the count of returned output values
         * @param _output the variant containing the output values
         * 
         * @return UA_StatusCode the status whether the method call was successful
         */
        UA_StatusCode instruct(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, size_t* _output_size, UA_Variant** _output) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "INSTRUCTIONS: Instruct robot on position %d with port %d to cook recipe %d after step %d", position_, port_, _recipe_id, _processed_steps);
            method_node_caller receive_robot_task_caller;
            receive_robot_task_caller.add_scalar_input_argument(&_recipe_id, UA_TYPES_UINT32);
            receive_robot_task_caller.add_scalar_input_argument(&_processed_steps, UA_TYPES_UINT32);
            object_method_info omi = method_id_map_[RECEIVE_TASK];
            if (omi == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECEIVE_TASK);
                return UA_STATUSCODE_BAD;
            }
            UA_StatusCode status = receive_robot_task_caller.call_method_node(client_, omi.object_id_, omi.method_id_, _output_size, _output);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
            }
            return status;
        }
};

struct plate {
    private:
        const plate_id_t id_;
        position_t position_;
        recipe_id_t placed_recipe_id_;
        UA_UInt32 processed_steps_of_placed_recipe_id_;
        UA_Boolean occupied_;
        remote_robot* target_robot_;
        std::string instance_name_id_;
        object_type_node_inserter& plate_type_inserter_;
    public:
        /**
         * @brief Setup the plate object type
         * 
         * @param _plate_type_inserter the plate type inserter
         * @param _conveyor the conveyor server
         * @return UA_StatusCode the status code
         */
        static UA_StatusCode setup_plate_object_type(object_type_node_inserter& _plate_type_inserter, UA_Server* _conveyor) {
            UA_StatusCode status;
            /* Add attributes */
            status = _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_ID);
            status |= _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_POSITION);
            status |= _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_RECIPE_ID);
            status |= _plate_type_inserter.add_attribute(PLATE_TYPE, PLATE_OCCUPIED);
            /* Add plate type constructor */
            status |= _plate_type_inserter.add_object_type_constructor(_conveyor, _plate_type_inserter.get_object_type_id(PLATE_TYPE));
            return status;
        }

        /**
         * @brief Construct a new plate object.
         * 
         * @param _id the plate id
         * @param _position the plate position
         * @param _conveyor_instance_id the conveyor instance id
         * @param _plate_type_inserter the plate type inserter
         */
        plate(plate_id_t _id, position_t _position, UA_NodeId _conveyor_instance_id, object_type_node_inserter& _plate_type_inserter) : id_(_id), position_(_position), placed_recipe_id_(0),
                processed_steps_of_placed_recipe_id_(0), occupied_(false), target_robot_(nullptr), instance_name_id_(std::string(PLATE_INSTANCE_NAME) + " " + std::to_string(id_)), plate_type_inserter_(_plate_type_inserter) {
            /* Instantiate plate type */
            UA_StatusCode status = plate_type_inserter_.add_object_instance(instance_name_id_.c_str(), PLATE_TYPE, _conveyor_instance_id, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding plate object instance");
                return;
            }
            /* Set attribute values */
            status = plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_ID, const_cast<plate_id_t*>(&id_), UA_TYPES_UINT32);
            status |= plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_POSITION, &position_, UA_TYPES_UINT32);
            status |= plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_RECIPE_ID, &placed_recipe_id_, UA_TYPES_UINT32);
            status |= plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_OCCUPIED, &occupied_, UA_TYPES_BOOLEAN);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting plate attributes");
            }
        }

        /**
         * @brief Destroy the plate object.
         * 
         */
        ~plate() {
        }

        /**
         * @brief Construct a new plate object.
         * 
         * @param _plate 
         */
        plate(const plate& _plate) : id_(_plate.id_), position_(_plate.position_), placed_recipe_id_(_plate.placed_recipe_id_), processed_steps_of_placed_recipe_id_(_plate.processed_steps_of_placed_recipe_id_),
            occupied_(_plate.occupied_), target_robot_(_plate.target_robot_), instance_name_id_(_plate.instance_name_id_), plate_type_inserter_(_plate.plate_type_inserter_) {
        }

        /**
         * @brief Get the plate id
         * 
         * @return plate_id_t 
         */
        plate_id_t get_plate_id() const {
            return id_;
        }

        /**
         * @brief Set the position and write to the information node.
         * 
         * @param _position the plate position
         */
        void set_position(position_t _position) {
            position_ = _position;
            plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_POSITION, &position_, UA_TYPES_UINT32);
        }

        /**
         * @brief Get the current position of the plate on the conveyor
         * 
         * @return position_t the current position on the conveyor
         */
        position_t get_position() const {
            return position_;
        }

        /**
         * @brief Places the finished dish on the plate.
         * 
         * @param _placed_recipe_id the placed dish's recipe id
         */
        void place_recipe_id(recipe_id_t _placed_recipe_id) {
            placed_recipe_id_ = _placed_recipe_id;
            plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_RECIPE_ID, &placed_recipe_id_, UA_TYPES_UINT32);
        }

        /**
         * @brief Get the placed recipe id
         * 
         * @return recipe_id_t the recipe id
         */
        recipe_id_t get_placed_recipe_id() const {
            return placed_recipe_id_;
        }

        void set_target_robot(remote_robot* _target_robot) {
            target_robot_ = _target_robot;
        }

        /**
         * @brief Get the target robot to where the dish has to be transferred
         * 
         * @return remote_robot* the target robot
         */
        remote_robot* get_target_robot() const {
            return target_robot_;
        }

        /**
         * @brief Sets the processed steps of the currently placed recipe id on the plate
         * 
         * @param _processed_steps_of_placed_recipe_id the processed steps
         */
        void set_processed_steps(UA_UInt32 _processed_steps_of_placed_recipe_id) {
            processed_steps_of_placed_recipe_id_ = _processed_steps_of_placed_recipe_id;
        }

        /**
         * @brief Returns the processed steps of the currently placed recipe id on the plate
         * 
         * @return UA_UInt32 the processed steps
         */
        UA_UInt32 get_processed_steps() const {
            return processed_steps_of_placed_recipe_id_;
        }

        /**
         * @brief Sets the plate's occupied state.
         * 
         * @param _occupied the occupied state
         */
        void set_occupied(UA_Boolean _occupied) {
            occupied_ = _occupied;
            plate_type_inserter_.set_scalar_attribute(instance_name_id_, PLATE_OCCUPIED, &occupied_, UA_TYPES_BOOLEAN);
        }

        /**
         * @brief Returns whether plate is occupied or not
         * 
         * @return UA_Boolean indicates whether plate is occupied or not
         */
        UA_Boolean is_occupied() {
            return occupied_;
        }
};

class conveyor {

enum state {
    IDLING,
    MOVING
};

private:
    /* conveyor related member variables */
    UA_Server* server_;
    port_t port_;
    object_type_node_inserter conveyor_type_inserter_;
    object_type_node_inserter plate_type_inserter_;
    volatile UA_Boolean running_;
    state state_status_;
    std::vector<plate> plates_;
    std::thread server_iterate_thread_;
    std::set<plate_id_t> occupied_plates_;
    std::unordered_map<position_t, plate_id_t> position_plate_id_map_;
    std::unordered_map<position_t, port_t> notifications_map_;
    std::unordered_map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_;

    /**
     * @brief Extracts the remote robot port and position on which a finished order is ready to be retrieved.
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
     * @param _robot_port the port from which the finished order notification is sent
     * @param _robot_position the position on which the finished order is ready to be retrieved
     * @param _output the output pointer to store return parameters
     */
    void
    handle_finished_order_notification(port_t _robot_port, position_t _robot_position, UA_Variant* _output);

    /**
     * @brief Timed callback to call handle_retrieve_finished_orders.
     * 
     * @param _server the server instance from which this method is called
     * @param _data the conveyor instance passed to the scheduling call
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
     * @param _steps the steps the conveyor has to move
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
     * @brief Idles if there are no notifications and occupied plates. Otherwise it processes the notifications and occupied plates.
     * 
     */
    void
    determine_next_movement();

    /**
     * @brief Extracts the robot port and position as well as the recipe id of the finished dish.
     * 
     * @param _client the client instance from which this method is called
     * @param _userdata the conveyor instance passed to the handover finished order call
     * @param _request_id 
     * @param _response the pointer to the returned parameters
     */
    static void
    handover_finished_order_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /**
     * @brief Retrieves finished orders if corresponding plate is not occupied and schedules the next movement.
     * 
     * @param _remote_robot_port the port from which the finished dish is retrieved
     * @param _remote_robot_position the position on which the finished dish is retrieved
     * @param _finished_recipe the recipe id of the finished dish
     * @param _processed_steps the steps count processed so far
     * @param _next_remote_robot_port the port of the next suitable robot
     * @param _next_remote_robot_position the position of the next suitable robot
     */
    void
    handle_handover_finished_order(port_t _remote_robot_port, position_t _remote_robot_position, recipe_id_t _finished_recipe, UA_UInt32 _processed_steps, port_t _next_remote_robot_port, position_t _next_remote_robot_position);

    /**
     * @brief Timed callback to call move_conveyor, deliver_finished_order and determine_next_movement.
     * 
     * @param _server the server instance from which this method is called
     * @param _data the conveyor instance passed to the scheduling call
     */
    static void
    perform_movement(UA_Server* _server, void* _data);

    /**
     * @brief Extracts the returned robot state parameters and updates plates
     * 
     * @param _output_size the count of returned output values
     * @param _output the variant containing the output values
     */
    void
    receive_robot_task_called(size_t _output_size, UA_Variant* _output);

    /**
     * @brief Timed callback to call determine_next_movement.
     * 
     * @param _server the server instance from which this method is called
     * @param _data the conveyor instance passed to the scheduling call
     */
    static void
    determine_next_movement_callback(UA_Server* _server, void* _data);

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
     * @param _port the conveyor port
     * @param _robot_count the robot count
     */
    conveyor(port_t _port, UA_UInt32 _robot_count);

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