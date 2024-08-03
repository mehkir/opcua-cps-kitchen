#include "../include/robot.hpp"
#include "node_ids.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>
#include <string>

robot::robot(uint16_t _robot_port, uint16_t _clock_port) : robot_server_(UA_Server_new()), robot_port_(_robot_port), clock_client_(UA_Client_new()), current_clock_tick_(0), next_clock_tick_(0), running_(true) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* robot_server_config = UA_Server_getConfig(robot_server_);
    status = UA_ServerConfig_setMinimal(robot_server_config, robot_port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        UA_Server_delete(robot_server_);
        return;
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

void
robot::clock_tick_notification_callback(UA_Client* client, UA_UInt32 subscription_id, void* subscription_context,
                                        UA_UInt32 monitor_id, void* monitor_context, UA_DataValue* value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);

        robot* self = static_cast<robot*>(monitor_context);
        self->handle_clock_tick_notification(new_clock_tick);
    }
}

void
robot::handle_clock_tick_notification(UA_UInt64 _new_clock_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    current_clock_tick_ = _new_clock_tick;
}

void
robot::receive_tick_ack_called(UA_Client* client, void* userdata, UA_UInt32 request_id, UA_CallResponse* response) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Userdata is NULL");
        return;
    }

    UA_StatusCode status_code = response->responseHeader.serviceResult;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad service result", __FUNCTION__);
        return;
    }

    UA_Boolean tick_ack_result;
    if(UA_Variant_hasScalarType(response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        tick_ack_result = *(UA_Boolean*)response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, tick_ack_result);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    robot* self = static_cast<robot*>(userdata);
    self->handle_receive_tick_ack_result(tick_ack_result);
}

void
robot::handle_receive_tick_ack_result(UA_Boolean _tick_ack_result) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
}

robot::~robot() {
    UA_Server_delete(robot_server_);
    UA_Client_delete(clock_client_);
}

void
robot::start() {
    next_clock_tick_ = rand() % 1000;
    receive_tick_ack_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);
    receive_tick_ack_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
    receive_tick_ack_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);
    UA_StatusCode status = receive_tick_ack_caller_.call_method_node(clock_client_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
        return;
    }

    clock_client_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_Client_run_iterate(clock_client_, 1000);
        }
    });

    status = UA_Server_run(robot_server_, &running_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the server");
        running_ = false;
    }
}

void
robot::stop() {
    running_ = false;
    clock_client_iterate_thread_.join();
}