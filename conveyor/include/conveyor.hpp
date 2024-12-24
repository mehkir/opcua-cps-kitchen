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
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "types.hpp"

struct plate {
    private:
        const plate_id_t plate_id_;
        position_t position_;
        UA_UInt32 placed_recipe_id_;
        UA_Boolean occupied_;
    public:
        plate(plate_id_t _plate_id, position_t _position) : plate_id_(_plate_id), position_(_position), placed_recipe_id_(0), occupied_(false) {
        }

        ~plate() {
        }

        plate_id_t get_plate_id() const {
            return plate_id_;
        }

        void set_position(position_t _position) {
            position_ = _position;
        }

        position_t get_position() {
            return position_;
        }

        void place_recipe_id(UA_UInt32 _placed_recipe_id) {
            placed_recipe_id_ = _placed_recipe_id;
        }

        UA_UInt32 get_placed_recipe_id() {
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
private:
    /* conveyor related member variables */
    UA_Server* server_;
    port_t port_;
    volatile UA_Boolean running_;
    std::vector<plate> plates_;
    std::thread server_iterate_thread_;
    method_node_inserter receive_finished_order_inserter_;
    std::set<plate_id_t> occupied_plates_;
    std::unordered_map<position_t, port_t> notifications_map_;

    /* Places a finished order on a plate */
    static UA_StatusCode
    receive_finished_order_notification(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);

    void
    handle_finished_order_notification(port_t _robot_port, position_t _robot_position);

    void
    move_conveyor(steps_t _steps);

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