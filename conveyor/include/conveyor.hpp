#ifndef CONVEYOR_HPP
#define CONVEYOR_HPP

#define OUTPUT_POSITION 0

#include <open62541/server.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <unordered_map>
#include <string>
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "client_connection_establisher.hpp"

struct plate {
    private:
        const UA_UInt32 plate_id_;
        UA_UInt32 position_;
        UA_UInt32 placed_recipe_id_;
        UA_Boolean busy_;
    public:
        plate(UA_UInt32 _plate_id, UA_UInt32 _position) : plate_id_(_plate_id), position_(_position), placed_recipe_id_(0), busy_(false) {
        }

        ~plate() {
        }

        UA_UInt32 get_plate_id() const {
            return plate_id_;
        }

        void set_position(UA_UInt32 _position) {
            position_ = _position;
        }

        UA_UInt32 get_position() {
            return position_;
        }

        void place_recipe_id(UA_UInt32 _placed_recipe_id) {
            placed_recipe_id_ = _placed_recipe_id;
        }

        UA_UInt32 get_placed_recipe_id() {
            return placed_recipe_id_;
        }

        void set_busy_state(UA_Boolean _busy) {
            busy_ = _busy;
        }

        UA_Boolean get_busy_state() {
            return busy_;
        }
};

class conveyor {
private:
    /* conveyor related member variables */
    UA_Server* server_;
    UA_UInt16 port_;
    volatile UA_Boolean running_;
    std::vector<plate> plates_;
    std::thread server_iterate_thread_;
    method_node_inserter receive_finished_order_inserter_;
    /* controller related member variables */
    UA_Client* controller_client_;

    /* Places a finished order on a plate */
    static UA_StatusCode
    receive_finished_order_notification(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);

    void
    move_conveyor(UA_UInt32 _steps);

    void
    join_threads();

public:
    conveyor(UA_UInt16 _port, UA_UInt32 _robot_count);
    ~conveyor();

    void
    start();

    void
    stop();
};

#endif // CONVEYOR_HPP