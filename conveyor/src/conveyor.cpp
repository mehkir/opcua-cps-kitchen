#include "../include/conveyor.hpp"
#include <open62541/server_config_default.h>
#include "node_ids.hpp"

#include <string>
#include <memory>

conveyor::conveyor(UA_UInt16 _conveyor_port, UA_UInt16 _robot_start_port, UA_UInt32 _robot_count, UA_UInt32 _plates_count, UA_UInt16 _clock_port, UA_UInt16 _controller_port) : conveyor_port_(_conveyor_port), running_(true), current_clock_tick_(0), next_clock_tick_(0), clock_client_(UA_Client_new()) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;

    UA_ServerConfig* conveyor_server_config = UA_Server_getConfig(conveyor_server_);
    status = UA_ServerConfig_setMinimal(conveyor_server_config, conveyor_port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the conveyor server");
    }

    /* Run the conveyor server */
    conveyor_server_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_StatusCode status = UA_Server_run_iterate(conveyor_server_, true);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the conveyor server");
            }
        }
    });

    for (size_t i = 0; i < _robot_count; i++) {
        uint16_t remote_port = _robot_start_port + i;
        port_remote_robot_map_[remote_port] = std::make_unique<remote_robot>(remote_port);
        robot_position_to_port_[i+1] = remote_port;
    }

    /* Setup clock client */
    client_connection_establisher clock_client_connection_establisher;
    UA_SessionState clock_session_state = clock_client_connection_establisher.establish_connection(clock_client_, _clock_port);
    if (clock_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing clock client session");
    }

    if (clock_session_state == UA_SESSIONSTATE_ACTIVATED) {
        /* Run the clock client */
        clock_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(clock_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running the clock client");
                }
            }
        });

        /* Setup clock tick monitoring */
        status = clock_tick_subscriber_.subscribe_node_value(clock_client_, UA_NODEID_STRING(1, CLOCK_TICK), clock_tick_notification_callback, this);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error subscribing to the clock tick node");
        }

        /* Add receive tick ack method caller to send next tick */
        receive_tick_ack_caller_.add_input_argument(&conveyor_port_, UA_TYPES_UINT16);
        receive_tick_ack_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
        receive_tick_ack_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);
    }

    for (size_t i = 0; i < _plates_count; i++) {
        plates_.push_back(plate(i,i,i));
    }

    /* Setup controller client */
    client_connection_establisher controller_client_connection_establisher;
    UA_SessionState controller_session_state = controller_client_connection_establisher.establish_connection(controller_client_, _controller_port);
    if (controller_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing controller client session");
    }

    receive_conveyor_state_caller_.add_input_argument(&plate_id_state_, UA_TYPES_UINT32);
    receive_conveyor_state_caller_.add_input_argument(&plate_busy_state_, UA_TYPES_BOOLEAN);
    receive_conveyor_state_caller_.add_input_argument(&plate_current_tick_state_, UA_TYPES_UINT64);
    receive_conveyor_state_caller_.add_input_argument(&plate_next_tick_state_, UA_TYPES_UINT64);

    receive_move_instruction_inserter.add_input_argument("steps to move", "steps_to_move", UA_TYPES_UINT32);
    receive_move_instruction_inserter.add_output_argument("steps to move received", "steps_to_move_received", UA_TYPES_BOOLEAN);
    status = receive_move_instruction_inserter.add_method_node(conveyor_server_, UA_NODEID_STRING(1, RECEIVE_MOVE_INSTRUCTION), "receive move instruction", receive_move_instruction, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive move instruction method node");
    }
}

conveyor::~conveyor() {
    UA_Server_delete(conveyor_server_);
    UA_Client_delete(clock_client_);
}

void
conveyor::clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                        UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) _value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);

        conveyor* self = static_cast<conveyor*>(_monitor_context);
        self->handle_clock_tick_notification(new_clock_tick);
    }
}

void
conveyor::handle_clock_tick_notification(UA_UInt64 _new_clock_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    current_clock_tick_ = _new_clock_tick;
    progress_new_tick(_new_clock_tick);
}

void
conveyor::receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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
    
    conveyor* self = static_cast<conveyor*>(_userdata);
    self->handle_receive_tick_ack_result(tick_ack_result);
}

void
conveyor::handle_receive_tick_ack_result(UA_Boolean _tick_ack_result) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
}

UA_StatusCode
conveyor::receive_move_instruction(UA_Server *_server,
        const UA_NodeId *_session_id, void *_session_context,
        const UA_NodeId *_method_id, void *_method_context,
        const UA_NodeId *_object_id, void *_object_context,
        size_t _input_size, const UA_Variant *_input,
        size_t _output_size, UA_Variant *_output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad input size");
        return UA_STATUSCODE_BAD;
    }
    UA_UInt32 steps_to_move = *(UA_UInt32*)_input[0].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    conveyor* self = static_cast<conveyor*>(_method_context);
    self->handle_receive_move_instruction(steps_to_move, _output);
    return UA_STATUSCODE_BAD;
}

void
conveyor::handle_receive_move_instruction(UA_UInt32 _steps_to_move, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    move_conveyor(_steps_to_move);

    next_clock_tick_++;
    UA_StatusCode status = receive_tick_ack_caller_.call_method_node(clock_client_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
    }
    
    UA_Boolean successfully_moved = true;
    status = UA_Variant_setScalarCopy(_output, &successfully_moved, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error returning receive move instuction ack");
    }
}

void
conveyor::move_conveyor(uint32_t steps) {
    for (size_t i = 0; i < plates_.size(); i++) {
        int new_position = (plates_[i].get_adjacent_robot_position() + steps) % plates_.size();
        plates_[i].set_adjacent_robot_position(new_position);
    }
}

void
conveyor::receive_conveyor_state_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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

    UA_Boolean conveyor_state_received;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        conveyor_state_received = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, conveyor_state_received);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    conveyor* self = static_cast<conveyor*>(_userdata);
    self->handle_receive_conveyor_state_result(conveyor_state_received);
}

void
conveyor::handle_receive_conveyor_state_result(UA_Boolean _conveyor_state_received) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if (!_conveyor_state_received)
        return;
}

void
conveyor::transmit_all_plate_states() {
    // TODO: Think of required states
    for (plate p : plates_) {
        plate_id_state_ = p.get_plate_id();
        plate_current_tick_state_ = current_clock_tick_;
        plate_busy_state_ = p.get_busy_state();
        UA_StatusCode status = receive_conveyor_state_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, RECEIVE_CONVEYOR_STATE), receive_conveyor_state_called, this);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
        }
    }
}

void
conveyor::progress_new_tick(UA_UInt64 _new_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s with new tick %lu", __FUNCTION__, _new_tick);
}

void
conveyor::start() {
    next_clock_tick_++; // TODO: Compute from move instruction
    transmit_all_plate_states();

    clock_client_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_Client_run_iterate(clock_client_, 1000);
        }
    });

    controller_client_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_Client_run_iterate(controller_client_, 1000);
        }
    });
}

void
conveyor::stop() {
    running_ = false;
    clock_client_iterate_thread_.join();
    controller_client_iterate_thread_.join();
}