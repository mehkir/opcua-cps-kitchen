#ifndef CONVEYOR_HPP
#define CONVEYOR_HPP

#define OUTPUT_POSITION 0

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <unordered_map>
#include <string>
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"

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
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error connecting to the robot server");
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

struct plate {
    private:
        const UA_UInt32 plate_id_;
        UA_UInt16 adjacent_robot_position_;
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
};

class conveyor {
private:
    /* conveyor related member variables */
    UA_UInt16 conveyor_port_;
    volatile UA_Boolean running_;
    std::vector<plate> plates_;
    std::unordered_map<UA_UInt32, UA_UInt16> robot_position_to_port_;
    /* clock related member variables */
    UA_Client* clock_client_;
    node_value_subscriber clock_tick_subscriber_;
    UA_UInt64 current_clock_tick_;
    UA_UInt64 next_clock_tick_;
    std::thread client_iterate_thread_;
    method_node_caller receive_tick_ack_caller_;
    /* robot related member variables */
    std::unordered_map<uint16_t, std::unique_ptr<remote_robot>> port_remote_robot_map_;

    static void
    clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                    UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value);
    void
    handle_clock_tick_notification(UA_UInt64 _new_clock_tick);

    static void
    receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    handle_receive_tick_ack_result(UA_Boolean _tick_ack_result);

    void
    move_conveyor(uint32_t steps);
public:
    conveyor(UA_UInt16 _conveyor_port, UA_UInt16 _robot_start_port, UA_UInt32 _robot_count, UA_UInt32 _plates_count, UA_UInt16 _clock_port);
    ~conveyor();

    void
    start();

    void
    stop();
};

#endif // CONVEYOR_HPP