#include "../include/controller.hpp"
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <string>

controller::controller(uint16_t _controller_port, uint16_t _start_port, uint32_t _robot_count, uint16_t _clock_port) : controller_port_(_controller_port), running_(true), clock_client_(UA_Client_new()), current_clock_tick_(0), next_clock_tick_(0) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    for (size_t i = 0; i < _robot_count; i++) {
        UA_Client* client = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));
        uint16_t remote_port = _start_port + i;
        std::string endpoint = "opc.tcp://localhost:" + std::to_string(remote_port);
        status = UA_Client_connect(client, endpoint.c_str());
        if(status != UA_STATUSCODE_GOOD) {
            UA_Client_delete(client);
        } else {
            port_remote_robot_map_[remote_port] = remote_robot(client, remote_port);
        }
    }

    UA_ClientConfig* clock_client_config = UA_Client_getConfig(clock_client_);
    clock_client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    std::string clock_endpoint = "opc.tcp://localhost:" + std::to_string(_clock_port);
    status = UA_Client_connect(clock_client_, clock_endpoint.c_str());
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error connecting to the clock server");
        UA_Client_delete(clock_client_);
        return;
    }

    status = clock_tick_subscriber_.subscribe_node_value(clock_client_, UA_NODEID_STRING(1, CLOCK_TICK), clock_tick_notification_callback, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error subscribing to the clock tick node");
        UA_Client_delete(clock_client_);
        return;
    }
}

controller::~controller() {
    UA_Client_delete(clock_client_);
}

void
controller::clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                        UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) _value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);

        controller* self = static_cast<controller*>(_monitor_context);
        self->handle_clock_tick_notification(new_clock_tick);
    }
}

void
controller::handle_clock_tick_notification(UA_UInt64 _new_clock_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    current_clock_tick_ = _new_clock_tick;
}

void
controller::receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Userdata is NULL");
        return;
    }

    UA_StatusCode status_code = _response->responseHeader.serviceResult;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad service result", __FUNCTION__);
        return;
    }

    UA_Boolean tick_ack_result;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        tick_ack_result = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, tick_ack_result);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    controller* self = static_cast<controller*>(_userdata);
    self->handle_receive_tick_ack_result(tick_ack_result);
}

void
controller::handle_receive_tick_ack_result(UA_Boolean _tick_ack_result) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
}

void
controller::start() {
    next_clock_tick_ = rand() % 1000;
    receive_tick_ack_caller_.add_input_argument(&controller_port_, UA_TYPES_UINT16);
    receive_tick_ack_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
    receive_tick_ack_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);
    UA_StatusCode status = receive_tick_ack_caller_.call_method_node(clock_client_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
        return;
    }

    for (std::pair<const uint16_t, remote_robot> port_remote_robot : port_remote_robot_map_) {
        port_client_thread_map_[port_remote_robot.first] = std::thread([&port_remote_robot, this]() {
            while(running_) {
                UA_Client_run_iterate(port_remote_robot.second.get_client(), 1000);
            }
        });
    }
    for (auto& port_client_thread : port_client_thread_map_) {
        port_client_thread.second.join();
    }
}

void
controller::stop() {
    running_ = false;
}