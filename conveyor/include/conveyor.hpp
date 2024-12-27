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
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "types.hpp"
#include "node_ids.hpp"

struct remote_robot {
    private:
        UA_Client* client_;
        const port_t port_;
        const position_t position_;
        bool running_;
        std::thread client_thread_;
        method_node_caller handover_finished_order_caller_;

    public:
        remote_robot(port_t _port, position_t _position) :  port_(_port), position_(_position), client_(UA_Client_new()), running_(true) {
            client_connection_establisher robot_client_connection_establisher;
            UA_SessionState session_state = robot_client_connection_establisher.establish_connection(client_, port_);
            if (session_state != UA_SESSIONSTATE_ACTIVATED) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing robot client session");
                running_ = false;
                return;
            }

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

        ~remote_robot() {
            running_ = false;
            if (client_thread_.joinable())
                client_thread_.join();
            UA_Client_delete(client_);
        }

        port_t get_port() const {
            return port_;
        }

        position_t get_position() const {
            return position_;
        }

        void handover_finished_order(UA_ClientAsyncCallCallback _callback, void* _userdata) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "HANDOVER: Retrieve finished order from robot on position %d with port %d", position_, port_);
            UA_StatusCode status = handover_finished_order_caller_.call_method_node(client_, UA_NODEID_STRING(1, HANDOVER_FINSIHED_ORDER), _callback, _userdata);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
                running_ = false;
            }
        }
};

struct plate {
    private:
        const plate_id_t plate_id_;
        position_t position_;
        recipe_id_t placed_recipe_id_;
        UA_Boolean occupied_;
    public:
        plate(plate_id_t _plate_id, position_t _position) : plate_id_(_plate_id), position_(_position), placed_recipe_id_(0), occupied_(false) {
        }

        ~plate() {
        }

        plate(const plate& _plate) : plate_id_(_plate.plate_id_), position_(_plate.position_), placed_recipe_id_(_plate.placed_recipe_id_), occupied_(_plate.occupied_) {
        }

        // plate& operator=(const plate& _plate) {
        //     if (this == &_plate) {
        //         return *this;
        //     }

        //     plate_id_ = _plate.plate_id_;
        //     position_ = _plate.position_;
        //     placed_recipe_id_ = _plate.placed_recipe_id_;
        //     occupied_ = _plate.occupied_;
        //     return *this;
        // }

        plate_id_t get_plate_id() const {
            return plate_id_;
        }

        void set_position(position_t _position) {
            position_ = _position;
        }

        position_t get_position() {
            return position_;
        }

        void place_recipe_id(recipe_id_t _placed_recipe_id) {
            placed_recipe_id_ = _placed_recipe_id;
        }

        recipe_id_t get_placed_recipe_id() {
            return placed_recipe_id_;
        }

        void set_occupied(UA_Boolean _occupied) {
            occupied_ = _occupied;
        }

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
    std::unordered_map<position_t, plate*> position_plates_map_;
    std::unordered_map<position_t, port_t> notifications_map_;
    std::unordered_map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_;

    static UA_StatusCode
    receive_finished_order_notification(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);

    void
    handle_finished_order_notification(port_t _robot_port, position_t _robot_position);

    static void
    retrieve_finished_orders(UA_Server* _server, void* _data);

    void
    handle_retrieve_finished_orders();

    void
    move_conveyor(steps_t _steps);

    void
    deliver_finished_order();

    void
    determine_next_movement();

    static void
    handover_finished_order_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_handover_finished_order(port_t _remote_robot_port, position_t _remote_robot_position, recipe_id_t _finished_recipe);

    static void
    perform_movement(UA_Server* _server, void* _data);

    void
    join_threads();

public:
    conveyor(port_t _port, UA_UInt32 _robot_count);
    ~conveyor();

    void
    start();

    void
    stop();
};

#endif // CONVEYOR_HPP