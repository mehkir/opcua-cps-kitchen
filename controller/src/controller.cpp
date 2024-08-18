#include "../include/controller.hpp"
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <string>

controller::controller(uint16_t _controller_port, uint16_t _robot_start_port, uint32_t _robot_count, uint16_t _conveyor_start_port, uint32_t _conveyor_count, uint16_t _clock_port) : controller_server_(UA_Server_new()), controller_port_(_controller_port), running_(true), clock_client_(UA_Client_new()), current_clock_tick_(0), next_clock_tick_(0) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* controller_server_config = UA_Server_getConfig(controller_server_);
    status = UA_ServerConfig_setMinimal(controller_server_config, controller_port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        return;
    }

    for (size_t i = 0; i < _robot_count; i++) {
        uint16_t remote_port = _robot_start_port + i;
        port_remote_robot_map_[remote_port] = std::make_unique<remote_robot>(remote_port);
    }

    for (size_t i = 0; i < _conveyor_count; i++) {
        uint16_t remote_port = _conveyor_start_port + i;
        port_remote_conveyor_map_[remote_port] = std::make_unique<remote_conveyor>(remote_port);
    }

    UA_ClientConfig* clock_client_config = UA_Client_getConfig(clock_client_);
    clock_client_config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    std::string clock_endpoint = "opc.tcp://localhost:" + std::to_string(_clock_port);
    status = UA_Client_connect(clock_client_, clock_endpoint.c_str());
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error connecting to the clock server");
        return;
    }

    status = clock_tick_subscriber_.subscribe_node_value(clock_client_, UA_NODEID_STRING(1, CLOCK_TICK), clock_tick_notification_callback, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error subscribing to the clock tick node");
        return;
    }

    receive_tick_ack_caller_.add_input_argument(&controller_port_, UA_TYPES_UINT16);
    receive_tick_ack_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
    receive_tick_ack_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);

    receive_robot_state_inserter_.add_input_argument("robot port", "port", UA_TYPES_UINT16);
    receive_robot_state_inserter_.add_input_argument("robot busy status", "busy", UA_TYPES_BOOLEAN);
    receive_robot_state_inserter_.add_input_argument("robot current tick", "current_tick", UA_TYPES_UINT64);
    receive_robot_state_inserter_.add_input_argument("robot next tick", "next_tick", UA_TYPES_UINT64);
    receive_robot_state_inserter_.add_output_argument("robot state received", "robot_state_received", UA_TYPES_BOOLEAN);
    status = receive_robot_state_inserter_.add_method_node(controller_server_, UA_NODEID_STRING(1, RECEIVE_ROBOT_STATE), "receive robot state", receive_robot_state, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive robot state method node");
        return;
    }

    receive_conveyor_state_inserter_.add_input_argument("conveyor port", "port", UA_TYPES_UINT16);
    receive_conveyor_state_inserter_.add_input_argument("conveyor busy status", "busy", UA_TYPES_BOOLEAN);
    receive_conveyor_state_inserter_.add_input_argument("conveyor current tick", "current_tick", UA_TYPES_UINT64);
    receive_conveyor_state_inserter_.add_input_argument("conveyor next tick", "next_tick", UA_TYPES_UINT64);
    receive_conveyor_state_inserter_.add_output_argument("conveyor state received", "conveyor_state_received", UA_TYPES_BOOLEAN);
    status = receive_conveyor_state_inserter_.add_method_node(controller_server_, UA_NODEID_STRING(1, RECEIVE_CONVEYOR_STATE), "receive conveyor state", receive_conveyor_state, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive conveyor state method node");
        return;
    }
}

controller::~controller() {
    UA_Server_delete(controller_server_);
    UA_Client_delete(clock_client_);
}

UA_StatusCode
controller::receive_robot_state(UA_Server *server,
        const UA_NodeId *session_id, void *session_context,
        const UA_NodeId *method_id, void *method_context,
        const UA_NodeId *object_id, void *object_context,
        size_t input_size, const UA_Variant *input,
        size_t output_size, UA_Variant *output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Extract input arguments */
    UA_UInt16 port = *(UA_UInt16*)input[0].data;
    UA_Boolean busy = *(UA_Boolean*)input[1].data;
    UA_UInt64 current_tick = *(UA_UInt64*)input[2].data;
    UA_UInt64 next_tick = *(UA_UInt64*)input[3].data;
    /* Extract method context */
    controller* self = static_cast<controller*>(method_context);
    self->handle_receive_robot_state(port, busy, current_tick, next_tick, output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_receive_robot_state(UA_UInt16 _port, UA_Boolean _busy, UA_UInt64 _current_tick, UA_UInt64 _next_tick, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(port_remote_robot_map_.find(_port) == port_remote_robot_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Robot with port %d not found", _port);
        return;
    }
    UA_Boolean robot_state_received = true;
    UA_Variant_setScalarCopy(_output, &robot_state_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    remote_robot& robot = port_remote_robot_map_[_port].operator*();
    robot.set_busy_status(_busy);
    robot.set_current_tick(_current_tick);
    robot.set_next_tick(_next_tick);
    received_robot_states_.insert(_port);
    if(received_robot_states_.size() == port_remote_robot_map_.size()) {
        handle_all_robot_states_received();
    }
}

void
controller::handle_all_robot_states_received() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    for(auto& port_robot_pair : port_remote_robot_map_) {
        remote_robot& robot = port_robot_pair.second.operator*();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Robot with port %d has current tick %lu and next tick %lu", robot.get_port(), robot.get_current_tick(), robot.get_next_tick());
    }
    received_robot_states_.clear();
}

UA_StatusCode
controller::receive_conveyor_state(UA_Server *server,
        const UA_NodeId *session_id, void *session_context,
        const UA_NodeId *method_id, void *method_context,
        const UA_NodeId *object_id, void *object_context,
        size_t input_size, const UA_Variant *input,
        size_t output_size, UA_Variant *output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Extract input arguments */
    if(input_size != 4) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad input size");
        return UA_STATUSCODE_BAD;
    }
    UA_UInt16 port = *(UA_UInt16*)input[0].data;
    UA_Boolean busy = *(UA_Boolean*)input[1].data;
    UA_UInt64 current_tick = *(UA_UInt64*)input[2].data;
    UA_UInt64 next_tick = *(UA_UInt64*)input[3].data;
    /* Extract method context */
    if(method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(method_context);
    self->handle_receive_conveyor_state(port, busy, current_tick, next_tick, output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_receive_conveyor_state(UA_UInt16 _port, UA_Boolean _busy, UA_UInt64 _current_tick, UA_UInt64 _next_tick, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(port_remote_conveyor_map_.find(_port) == port_remote_conveyor_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Conveyor with port %d not found", _port);
        return;
    }
    UA_Boolean conveyor_state_received = true;
    UA_Variant_setScalarCopy(_output, &conveyor_state_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    remote_conveyor& conveyor = port_remote_conveyor_map_[_port].operator*();
    conveyor.set_busy_status(_busy);
    conveyor.set_current_tick(_current_tick);
    conveyor.set_next_tick(_next_tick);
    received_conveyor_states_.insert(_port);
    if(received_conveyor_states_.size() == port_remote_conveyor_map_.size()) {
        handle_all_conveyor_states_received();
    }
}

void
controller::handle_all_conveyor_states_received() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    for(auto& port_conveyor_pair : port_remote_conveyor_map_) {
        remote_conveyor& conveyor = port_remote_conveyor_map_[port_conveyor_pair.first].operator*();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Conveyor with port %d has current tick %lu and next tick %lu", conveyor.get_port(), conveyor.get_current_tick(), conveyor.get_next_tick());
    }
    received_conveyor_states_.clear();
}

void
controller::clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                        UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(!UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT64])) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad data type");
            return;
    }
    UA_UInt64 new_clock_tick = *(UA_UInt64 *) _value->value.data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "New clock tick is: %lu", new_clock_tick);

    if(_monitor_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Monitor context is NULL");
        return;
    }
    controller* self = static_cast<controller*>(_monitor_context);
    self->handle_clock_tick_notification(new_clock_tick);
}

void
controller::handle_clock_tick_notification(UA_UInt64 _new_clock_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    current_clock_tick_ = _new_clock_tick;
}

void
controller::receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);

    UA_StatusCode status_code = _response->responseHeader.serviceResult;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad service result", __FUNCTION__);
        return;
    }

    UA_Boolean tick_ack_result;
    if(!UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    tick_ack_result = *(UA_Boolean*)_response->results[0].outputArguments->data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, tick_ack_result);
    
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Userdata is NULL");
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
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    // next_clock_tick_ = rand() % 1000;
    // status = receive_tick_ack_caller_.call_method_node(clock_client_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, this);
    // if(status != UA_STATUSCODE_GOOD) {
    //     UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
    //     return;
    // }

    clock_client_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_Client_run_iterate(clock_client_, 1000);
        }
    });

    status = UA_Server_run(controller_server_, &running_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the server");
        running_ = false;
    }
}

void
controller::stop() {
    running_ = false;
    clock_client_iterate_thread_.join();
}