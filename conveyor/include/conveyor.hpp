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

struct remote_robot {
    private:
        UA_Client* client_;
        UA_UInt16 port_;
        UA_Boolean busy_;
        UA_UInt64 current_tick_;
        UA_UInt64 next_tick_;
        bool running_;
        std::thread client_thread_;

    public:
        remote_robot(UA_UInt16 _port = 0) :  port_(_port), busy_(false), client_(UA_Client_new()), running_(true) {
            client_connection_establisher robot_client_connection_establisher;
            UA_SessionState session_state = robot_client_connection_establisher.establish_connection(client_, port_);
            if (session_state != UA_SESSIONSTATE_ACTIVATED) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing robot client session");
            }

            client_thread_ = std::thread([this]() {
                while(running_) {
                    UA_Client_run_iterate(client_, 1000);
                }
            });
        }

        ~remote_robot() {
            running_ = false;
            client_thread_.join();
            UA_Client_delete(client_);
        }

        void set_busy_status(UA_Boolean _busy_status) {
            busy_ = _busy_status;
        }

        void set_current_tick(UA_UInt64 _current_tick) {
            current_tick_ = _current_tick;
        }

        void set_next_tick(UA_UInt64 _next_tick) {
            next_tick_ = _next_tick;
        }

        UA_UInt16 get_port() {
            return port_;
        }

        UA_Boolean is_busy() const{
            return busy_;
        }

        UA_UInt64 get_current_tick() const{
            return current_tick_;
        }

        UA_UInt64 get_next_tick() const{
            return next_tick_;
        }
};

struct plate {
    private:
        const UA_UInt32 plate_id_;
        UA_UInt16 adjacent_robot_position_;
        UA_Boolean busy_;
    public:
        plate(uint32_t _plate_id, uint32_t _position, uint32_t _adjacent_robot_position) : plate_id_(_plate_id), adjacent_robot_position_(_adjacent_robot_position) {
        }

        ~plate() {
        }

        void set_adjacent_robot_position(UA_UInt16 _adjacent_robot_position) {
            adjacent_robot_position_ = _adjacent_robot_position;
        }

        UA_UInt32 get_plate_id() const {
            return plate_id_;
        }

        UA_UInt16 get_adjacent_robot_position() {
            return adjacent_robot_position_;
        }

        UA_Boolean get_busy_state() {
            return busy_;
        }

        UA_Boolean set_busy_state(UA_Boolean _busy) {
            busy_ = _busy;
        }
};

class conveyor {
private:
    /* conveyor related member variables */
    UA_Server* conveyor_server_;
    UA_UInt16 conveyor_port_;
    volatile UA_Boolean running_;
    std::vector<plate> plates_;
    std::unordered_map<UA_UInt32, UA_UInt16> robot_position_to_port_;
    method_node_inserter receive_move_instruction_inserter;
    std::thread conveyor_server_iterate_thread_;
    /* clock related member variables */
    UA_Client* clock_client_;
    node_value_subscriber clock_tick_subscriber_;
    UA_UInt64 current_clock_tick_;
    UA_UInt64 next_clock_tick_;
    std::thread clock_client_iterate_thread_;
    method_node_caller receive_tick_ack_caller_;
    /* robot related member variables */
    std::unordered_map<uint16_t, std::unique_ptr<remote_robot>> port_remote_robot_map_;
    /* controller related member variables */
    UA_Client* controller_client_;
    method_node_caller receive_conveyor_state_caller_;
    std::thread controller_client_iterate_thread_;
    UA_UInt32 plate_id_state_;
    UA_Boolean plate_busy_state_;
    UA_UInt64 plate_current_tick_state_;
    UA_UInt64 plate_next_tick_state_;

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

    static void
    receive_conveyor_state_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_receive_conveyor_state_result(UA_Boolean _conveyor_state_received);

    void
    transmit_all_plate_states();

    void
    progress_new_tick(UA_UInt64 _new_tick);

public:
    conveyor(UA_UInt16 _conveyor_port, UA_UInt16 _robot_start_port, UA_UInt32 _robot_count, UA_UInt32 _plates_count, UA_UInt16 _clock_port, UA_UInt16 _controller_port);
    ~conveyor();

    void
    start();

    void
    stop();
};

#endif // CONVEYOR_HPP