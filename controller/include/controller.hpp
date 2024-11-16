#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <open62541/server.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <unordered_map>
#include <set>
#include <memory>
#include "node_value_subscriber.hpp"
#include "node_ids.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "information_node_inserter.hpp"


struct remote_robot {
    private:
        UA_Client* client_;
        UA_UInt16 port_;
        UA_Boolean busy_;
        bool running_;
        std::thread client_thread_;
        method_node_caller receive_robot_task_caller_;
        UA_UInt32 recipe_id_;

    public:
        remote_robot(UA_UInt16 _port = 0) :  port_(_port), busy_(false), client_(UA_Client_new()), running_(true) {
            client_connection_establisher robot_client_connection_establisher;
            UA_SessionState session_state = robot_client_connection_establisher.establish_connection(client_, port_);
            if (session_state != UA_SESSIONSTATE_ACTIVATED) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing robot client session");
            }

            receive_robot_task_caller_.add_input_argument(&recipe_id_, UA_TYPES_UINT32);

            client_thread_ = std::thread([this]() {
                while(running_) {
                    UA_Client_run_iterate(client_, 100);
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

        UA_UInt16 get_port() {
            return port_;
        }

        UA_Boolean is_busy() const{
            return busy_;
        }

        void instruct(UA_UInt32 _recipe_id, UA_ClientAsyncCallCallback _callback, void* _userdata) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
            recipe_id_ = _recipe_id;
            UA_StatusCode status = receive_robot_task_caller_.call_method_node(client_, UA_NODEID_STRING(1, RECEIVE_TASK), _callback, _userdata);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
                running_ = false;
            }
        }
};

struct remote_conveyor {
    private:
        UA_Client* client_;
        UA_UInt16 port_;
        UA_Boolean busy_;
        UA_UInt64 current_tick_;
        UA_UInt64 next_tick_;
        bool running_;
        std::thread client_thread_;
        method_node_caller receive_conveyor_move_instruction_caller_;
        UA_UInt32 steps_to_move_;

    public:
        remote_conveyor(UA_UInt16 _port = 0) :  port_(_port), busy_(false), client_(UA_Client_new()), running_(true) {
            client_connection_establisher conveyor_client_connection_establisher;
            UA_SessionState session_state = conveyor_client_connection_establisher.establish_connection(client_, port_);
            if (session_state != UA_SESSIONSTATE_ACTIVATED) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing conveyor client session");
            }

            receive_conveyor_move_instruction_caller_.add_input_argument(&steps_to_move_, UA_TYPES_UINT32);

            client_thread_ = std::thread([this]() {
                while(running_) {
                    UA_Client_run_iterate(client_, 100);
                }
            });
        }

        ~remote_conveyor() {
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

        void instruct(UA_UInt32 _steps_to_move, UA_ClientAsyncCallCallback _callback, void* _userdata) {
            steps_to_move_ = _steps_to_move;
            UA_StatusCode status = receive_conveyor_move_instruction_caller_.call_method_node(client_, UA_NODEID_STRING(1, RECEIVE_MOVE_INSTRUCTION), _callback, _userdata);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
                running_ = false;
            }
        }
};

struct remote_plate {
    private:
        const UA_UInt32 id_;
        UA_UInt16 adjacent_robot_position_;
        UA_Boolean busy_;
    public:
        remote_plate(uint32_t _id) : id_(_id) {
        }

        ~remote_plate() {
        }

        UA_UInt32 get_id() const {
            return id_;
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

class controller {
private:
    /* controller related member variables */
    UA_Server* controller_server_;
    UA_UInt16 controller_port_;
    volatile UA_Boolean running_;
    std::thread controller_server_iterate_thread_;
    std::set<UA_UInt16> received_proceeded_to_next_tick_notifications_;
    method_node_inserter receive_proceeded_to_next_tick_notification_inserter_;
    UA_Boolean place_remove_finished_order_notification_;
    information_node_inserter place_remove_finished_order_notification_node_inserter_;
    /* robot related member variables */
    std::unordered_map<uint16_t, std::unique_ptr<remote_robot>> port_remote_robot_map_;
    std::set<UA_UInt16> received_robot_states_;
    method_node_inserter receive_robot_state_inserter_;
    /* conveyor related member variables */
    std::unique_ptr<remote_conveyor> remote_conveyor_;
    std::vector<remote_plate> remote_plates_;
    std::set<UA_UInt16> received_conveyor_states_;
    method_node_inserter receive_conveyor_state_inserter_;

    static UA_StatusCode
    receive_robot_state(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    void
    handle_robot_state(UA_UInt16 _port, UA_Boolean _busy, UA_Variant* _output);

    void
    handle_all_robot_states_received();

    static void
    receive_robot_task_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_receive_robot_task_called_result(UA_Boolean _robot_task_sent);

    static UA_StatusCode
    receive_conveyor_state(UA_Server *_server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    void
    handle_conveyor_state(UA_UInt32 _plate_id, UA_Boolean _busy, UA_UInt16 _adjacent_robot_position, UA_Variant* _output);

    void    
    handle_all_conveyor_states_received();

    static void
    receive_conveyor_instruction_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_receive_conveyor_instruction_called_result(UA_Boolean _controller_state_received);

    static UA_StatusCode
    receive_proceeded_to_next_tick_notification(UA_Server *_server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    void handle_proceeded_to_next_tick_notification(UA_UInt16 _port, UA_Variant* _output);

public:
    controller(uint16_t _controller_port, uint16_t _robot_start_port, uint32_t _robot_count, uint16_t _remote_conveyor_port, uint16_t _clock_port);
    ~controller();

    void
    start();

    void
    stop();
};

#endif // CONTROLLER_HPP