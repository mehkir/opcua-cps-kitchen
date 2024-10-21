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
        UA_UInt16 adjacent_robot_position_;
        UA_Boolean busy_;
    public:
        plate(uint32_t _plate_id, uint32_t _adjacent_robot_position) : plate_id_(_plate_id), adjacent_robot_position_(_adjacent_robot_position), busy_(false) {
        }

        ~plate() {
        }

        UA_UInt32 get_plate_id() const {
            return plate_id_;
        }

        void set_adjacent_robot_position(UA_UInt16 _adjacent_robot_position) {
            adjacent_robot_position_ = _adjacent_robot_position;
        }

        UA_UInt16 get_adjacent_robot_position() {
            return adjacent_robot_position_;
        }

        UA_Boolean set_busy_state(UA_Boolean _busy) {
            busy_ = _busy;
        }

        UA_Boolean get_busy_state() {
            return busy_;
        }
};

struct robot_position_mapping {
    private:
        std::unordered_map<UA_UInt16, UA_UInt32> robot_port_to_position_;
        std::unordered_map<UA_UInt32, UA_UInt16> position_to_robot_port_;
        std::unordered_map<UA_UInt32, plate*> position_to_plate_;
    public:
        void add_robot_port_to_position_mapping(UA_UInt16 _robot_port, UA_UInt32 _position) {
            robot_port_to_position_[_robot_port] = _position;
        }

        void add_position_to_robot_port_mapping(UA_UInt32 _position, UA_UInt16 _robot_port) {
            position_to_robot_port_[_position] = _robot_port;
        }

        void add_position_to_plate_mapping(UA_UInt32 _position, plate* _plate) {
            position_to_plate_[_position] = _plate;
        }

        UA_UInt32 get_robot_position(UA_UInt16 _robot_port) {
            return robot_port_to_position_[_robot_port];
        }

        UA_UInt16 get_robot_port(UA_UInt32 _position) {
            return position_to_robot_port_[_position];
        }

        plate* get_plate(UA_UInt32 _positon) {
            return position_to_plate_[_positon];
        }

        plate* get_plate(UA_UInt16 _robot_port) {
            return position_to_plate_[robot_port_to_position_[_robot_port]];
        }
};

class conveyor {
private:
    /* conveyor related member variables */
    UA_Server* conveyor_server_;
    UA_UInt16 conveyor_port_;
    volatile UA_Boolean running_;
    std::vector<plate> plates_;
    robot_position_mapping robot_position_mapping_;
    method_node_inserter receive_move_instruction_inserter_;
    method_node_inserter place_finished_order_inserter_;
    std::thread conveyor_server_iterate_thread_;
    /* clock related member variables */
    UA_Client* clock_client_;
    node_value_subscriber clock_tick_subscriber_;
    UA_UInt64 current_clock_tick_;
    UA_UInt64 next_clock_tick_;
    std::thread clock_client_iterate_thread_;
    method_node_caller receive_tick_ack_caller_;
    /* controller related member variables */
    UA_Client* controller_client_;
    node_value_subscriber place_remove_finished_order_notification_subscriber_;
    method_node_caller receive_proceeded_to_next_tick_notification_caller_;
    /* Sends the conveyor state to the controller */
    method_node_caller receive_conveyor_state_caller_;
    std::thread controller_client_iterate_thread_;
    UA_UInt32 plate_id_state_;
    UA_Boolean plate_busy_state_;
    UA_UInt64 plate_current_tick_state_;
    UA_UInt16 plate_adjacent_robot_position_;

    static void
    clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                    UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value);
    void
    handle_clock_tick_notification(UA_UInt64 _new_clock_tick);

    static void
    receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_receive_tick_ack_result(UA_Boolean _tick_ack_result);

    static UA_StatusCode
    receive_move_instruction(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);
    
    void
    handle_receive_move_instruction(UA_UInt32 _steps_to_move, UA_Variant* _output);

    void
    move_conveyor(uint32_t steps);

    /* Receives the controller response to the sent conveyor state */
    static void
    receive_conveyor_state_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /* Handles the controller response to the sent conveyor state */
    void
    handle_conveyor_state_result(UA_Boolean _conveyor_state_received);

    /* Receives the controller response to the sent 'proceeded to next tick notification' */
    static void
    receive_proceeded_to_next_tick_notification_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /* Handles the controller reponse to the sent 'proceeded to next tick notification' */
    void
    handle_proceeded_to_next_tick_notification_result(UA_Boolean _proceeded_to_next_tick_notification_received);

    static void
    place_remove_finished_order_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                                    UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value);

    void
    handle_place_remove_finished_order_notification(UA_Boolean _place_remove_finished_order);

    /* Places a finished order on a plate */
    static UA_StatusCode
    place_finished_order(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output);
    
    void
    handle_place_finished_order(UA_UInt16 _robot_port, UA_UInt32 _procesed_order_id, UA_Variant* _output);

    void
    transmit_all_plate_states();

    void
    progress_new_tick(UA_UInt64 _new_tick);

public:
    conveyor(UA_UInt16 _conveyor_port, UA_UInt16 _robot_start_port, UA_UInt32 _robot_count, UA_UInt16 _clock_port, UA_UInt16 _controller_port);
    ~conveyor();

    void
    start();

    void
    stop();
};

#endif // CONVEYOR_HPP