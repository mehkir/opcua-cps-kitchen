#include "../include/conveyor.hpp"
#include "node_ids.hpp"

#include <string>

conveyor::conveyor(UA_UInt16 _conveyor_port, UA_UInt32 _robot_count, UA_UInt16 _clock_port) : conveyor_port_(_conveyor_port), running_(true), current_clock_tick_(0), next_clock_tick_(0), clock_client_(UA_Client_new()) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;

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

conveyor::~conveyor() {
}

void
conveyor::clock_tick_notification_callback(UA_Client* client, UA_UInt32 subscription_id, void* subscription_context,
                                        UA_UInt32 monitor_id, void* monitor_context, UA_DataValue* value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);

        conveyor* self = static_cast<conveyor*>(monitor_context);
        self->handle_clock_tick_notification(new_clock_tick);
    }
}

void
conveyor::handle_clock_tick_notification(UA_UInt64 _new_clock_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    current_clock_tick_ = _new_clock_tick;
}

void
conveyor::receive_tick_ack_called(UA_Client* client, void* userdata, UA_UInt32 request_id, UA_CallResponse* response) {
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
    
    conveyor* self = static_cast<conveyor*>(userdata);
    self->handle_receive_tick_ack_result(tick_ack_result);
}

void
conveyor::handle_receive_tick_ack_result(UA_Boolean _tick_ack_result) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
}

void
conveyor::start() {
    next_clock_tick_ = rand() % 1000;
    receive_tick_ack_caller_.add_input_argument(&conveyor_port_, UA_TYPES_UINT16);
    receive_tick_ack_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
    receive_tick_ack_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);
    UA_StatusCode status = receive_tick_ack_caller_.call_method_node(clock_client_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
        return;
    }
}

void
conveyor::stop() {
    running_ = false;
}