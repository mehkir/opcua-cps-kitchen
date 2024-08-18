#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <open62541/server.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <thread>
#include <unordered_map>
#include <set>
#include <memory>
#include "node_value_subscriber.hpp"
#include "node_ids.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"


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
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            UA_ClientConfig* client_config = UA_Client_getConfig(client_);
            client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
            std::string endpoint = "opc.tcp://localhost:" + std::to_string(port_);
            status = UA_Client_connect(client_, endpoint.c_str());
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error connecting to the clock server");
                return;
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

struct remote_conveyor {
    private:
        UA_Client* client_;
        UA_UInt16 port_;
        UA_Boolean busy_;
        UA_UInt64 current_tick_;
        UA_UInt64 next_tick_;
        bool running_;
        std::thread client_thread_;

    public:
        remote_conveyor(UA_UInt16 _port = 0) :  port_(_port), busy_(false), client_(UA_Client_new()), running_(true) {
            UA_StatusCode status = UA_STATUSCODE_GOOD;
            UA_ClientConfig* client_config = UA_Client_getConfig(client_);
            client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
            std::string endpoint = "opc.tcp://localhost:" + std::to_string(port_);
            status = UA_Client_connect(client_, endpoint.c_str());
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error connecting to the clock server");
                return;
            }

            client_thread_ = std::thread([this]() {
                while(running_) {
                    UA_Client_run_iterate(client_, 1000);
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
};

class controller {
private:
    /* controller related member variables */
    UA_Server* controller_server_;
    UA_UInt16 controller_port_;
    volatile UA_Boolean running_;
    /* robot related member variables */
    std::unordered_map<uint16_t, std::unique_ptr<remote_robot>> port_remote_robot_map_;
    std::set<UA_UInt16> received_robot_states_;
    method_node_inserter receive_robot_state_inserter_;
    /* conveyor related member variables */
    std::unordered_map<uint16_t, std::unique_ptr<remote_conveyor>> port_remote_conveyor_map_;
    std::set<UA_UInt16> received_conveyor_states_;
    method_node_inserter receive_conveyor_state_inserter_;
    /* clock related member variables */
    UA_Client* clock_client_;
    UA_Int64 current_clock_tick_;
    UA_Int64 next_clock_tick_;
    std::thread clock_client_iterate_thread_;
    node_value_subscriber clock_tick_subscriber_;
    method_node_caller receive_tick_ack_caller_;
    

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
    receive_robot_state(UA_Server *server,
            const UA_NodeId *session_id, void *session_context,
            const UA_NodeId *method_id, void *method_context,
            const UA_NodeId *object_id, void *object_context,
            size_t input_size, const UA_Variant *input,
            size_t output_size, UA_Variant *output);

    void
    handle_receive_robot_state(UA_UInt16 _port, UA_Boolean _busy, UA_UInt64 _current_tick, UA_UInt64 _next_tick, UA_Variant* _output);

    void
    handle_all_robot_states_received();

    static UA_StatusCode
    receive_conveyor_state(UA_Server *server,
            const UA_NodeId *session_id, void *session_context,
            const UA_NodeId *method_id, void *method_context,
            const UA_NodeId *object_id, void *object_context,
            size_t input_size, const UA_Variant *input,
            size_t output_size, UA_Variant *output);

    void
    handle_receive_conveyor_state(UA_UInt16 _port, UA_Boolean _busy, UA_UInt64 _current_tick, UA_UInt64 _next_tick, UA_Variant* _output);

    void    
    handle_all_conveyor_states_received();
public:
    controller(uint16_t _controller_port, uint16_t _robot_start_port, uint32_t _robot_count, uint16_t _conveyor_start_port, uint32_t _conveyor_count, uint16_t _clock_port);
    ~controller();

    void
    start();

    void
    stop();
};

#endif // CONTROLLER_HPP