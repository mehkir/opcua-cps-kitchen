#ifndef CONVEYOR_HPP
#define CONVEYOR_HPP

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
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "types.hpp"
#include "node_ids.hpp"
#include "information_node_inserter.hpp"
#include "information_node_writer.hpp"
#include "node_value_subscriber.hpp"
#include "robot_tool.hpp"

using namespace cps_kitchen;

struct remote_robot {
    private:
        UA_Client* client_;
        const port_t port_;
        const position_t position_;
        bool running_;
        std::thread client_thread_;
        method_node_caller handover_finished_order_caller_;
        method_node_caller receive_robot_task_caller_;
        recipe_id_t recipe_id_;
        UA_UInt32 processed_steps_;

    public:
        /**
         * @brief Construct a new remote robot object.
         * 
         * @param _port the port of the remote robot
         * @param _position the position of the remote robot
         */
        remote_robot(port_t _port, position_t _position) :  port_(_port), position_(_position), client_(UA_Client_new()), running_(true) {
            client_connection_establisher robot_client_connection_establisher;
            UA_SessionState session_state = robot_client_connection_establisher.establish_connection(client_, port_);
            if (session_state != UA_SESSIONSTATE_ACTIVATED) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing robot client session");
                running_ = false;
                return;
            }

            receive_robot_task_caller_.add_scalar_input_argument(&recipe_id_, UA_TYPES_UINT32);
            receive_robot_task_caller_.add_scalar_input_argument(&processed_steps_, UA_TYPES_UINT32);

            client_thread_ = std::thread([this]() {
                while(running_) {
                    UA_StatusCode status = UA_Client_run_iterate(client_, 100);
                    if (status != UA_STATUSCODE_GOOD) {
                        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running robot client");
                        running_ = false;
                    }
                }
            });
        }

        /**
         * @brief Destroy the remote robot object.
         * 
         */
        ~remote_robot() {
            running_ = false;
            if (client_thread_.joinable())
                client_thread_.join();
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
         * @param _callback the callback called after the robot is notified to hand over the finished order
         * @param _userdata the conveyor client
         */
        void handover_finished_order(UA_ClientAsyncCallCallback _callback, void* _userdata) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "HANDOVER: Retrieve finished order from robot on position %d with port %d", position_, port_);
            UA_StatusCode status = handover_finished_order_caller_.call_method_node(client_, UA_NODEID_STRING(1, const_cast<char*>(HANDOVER_FINISHED_ORDER)), _callback, _userdata);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
                running_ = false;
            }
        }

        /**
         * @brief Instructs the remote robot to process a partially processed dish.
         * 
         * @param _recipe_id the recipe ID of the dish
         * @param _processed_steps the processed steps of the recipe ID so far
         * @param _callback the callback called after the robot is instructed
         * @param _userdata the conveyor client
         */
        void instruct(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_ClientAsyncCallCallback _callback, void* _userdata) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            recipe_id_ = _recipe_id;
            processed_steps_ = _processed_steps;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "INSTRUCTIONS: Instruct robot on position %d with port %d to cook recipe %d after step %d", position_, port_, _recipe_id, _processed_steps);
            UA_StatusCode status = receive_robot_task_caller_.call_method_node(client_, UA_NODEID_STRING(1, const_cast<char*>(RECEIVE_TASK)), _callback, _userdata);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
                running_ = false;
            }
        }
};

struct plate {
    private:
        const plate_id_t id_;
        position_t position_;
        UA_Server* conveyor_;
        recipe_id_t placed_recipe_id_;
        UA_UInt32 processed_steps_of_placed_recipe_id_;
        UA_Boolean occupied_;
        remote_robot* target_robot_;
    public:
        /**
         * @brief Construct a new plate object.
         * 
         * @param _id the plate id
         * @param _position the plate position
         * @param _conveyor the reference to the conveyor 
         */
        plate(plate_id_t _id, position_t _position, UA_Server* _conveyor) : id_(_id), position_(_position), conveyor_(_conveyor), placed_recipe_id_(0), processed_steps_of_placed_recipe_id_(0), occupied_(false), target_robot_(nullptr) {
            information_node_inserter inserter;
            std::string id_node_id = "plate_id_" + std::to_string(id_);
            UA_StatusCode status = inserter.add_scalar_node(conveyor_, UA_NODEID_STRING(1, const_cast<char*>(id_node_id.c_str())), "plate id", UA_TYPES_UINT32, const_cast<plate_id_t*>(&id_));
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the plate id information node", __FUNCTION__);
            }

            std::string position_node_id = "plate_position_" + std::to_string(id_);
            status = inserter.add_scalar_node(conveyor_, UA_NODEID_STRING(1, const_cast<char*>(position_node_id.c_str())), "plate position", UA_TYPES_UINT32, &position_);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the plate position information node", __FUNCTION__);
            }

            std::string placed_recipe_id_node_id = "plate_placed_recipe_id_" + std::to_string(id_);
            status = inserter.add_scalar_node(conveyor_, UA_NODEID_STRING(1, const_cast<char*>(placed_recipe_id_node_id.c_str())), "placed recipe id", UA_TYPES_UINT32, &placed_recipe_id_);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the plate placed recipe id information node", __FUNCTION__);
            }

            std::string occupied_id_node_id = "plate_occupied_" + std::to_string(id_);
            status = inserter.add_scalar_node(conveyor_, UA_NODEID_STRING(1, const_cast<char*>(occupied_id_node_id.c_str())), "plate occupied status", UA_TYPES_BOOLEAN, &occupied_);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the plate occupied information node", __FUNCTION__);
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
        plate(const plate& _plate) : id_(_plate.id_), position_(_plate.position_), conveyor_(_plate.conveyor_), placed_recipe_id_(_plate.placed_recipe_id_), occupied_(_plate.occupied_) {
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
            std::string position_node_id = "plate_position_" + std::to_string(id_);
            information_node_writer position_writer;
            UA_StatusCode status = position_writer.write_value(conveyor_, UA_NODEID_STRING(1, const_cast<char*>(position_node_id.c_str())), &position_, &UA_TYPES[UA_TYPES_UINT32]);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Plate position write failed", __FUNCTION__);
            }
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
            std::string placed_recipe_id_node_id = "plate_placed_recipe_id_" + std::to_string(id_);
            information_node_writer placed_recipe_id_writer;
            UA_StatusCode status = placed_recipe_id_writer.write_value(conveyor_, UA_NODEID_STRING(1, const_cast<char*>(placed_recipe_id_node_id.c_str())), &placed_recipe_id_, &UA_TYPES[UA_TYPES_UINT32]);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Plate placed recipe write failed", __FUNCTION__);
            }
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
            std::string occupied_id_node_id = "plate_occupied_" + std::to_string(id_);
            information_node_writer occupied_writer;
            UA_StatusCode status = occupied_writer.write_value(conveyor_, UA_NODEID_STRING(1, const_cast<char*>(occupied_id_node_id.c_str())), &occupied_, &UA_TYPES[UA_TYPES_BOOLEAN]);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Plate occupied write failed", __FUNCTION__);
            }
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
    volatile UA_Boolean running_;
    state state_status_;
    std::vector<plate> plates_;
    std::thread server_iterate_thread_;
    method_node_inserter receive_finished_order_notification_inserter_;
    std::set<plate_id_t> occupied_plates_;
    std::set<position_t> retrievable_positions_;
    std::set<position_t> retrieved_positions_;
    std::set<position_t> deliverable_positions_;
    std::set<position_t> delivered_positions_;
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
     * @brief Callback called after robot is instructed. Extracts the returned robot state parameters.
     * 
     * @param _client the client instance from which this method is called
     * @param _userdata the userdata passed to the instruct call
     * @param _request_id 
     * @param _response the pointer to the returned parameters
     */
    static void
    receive_robot_task_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

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