#include "../include/controller.hpp"
#include <open62541/server_config_default.h>
#include <string>

controller::controller(uint16_t _controller_port, uint16_t _robot_start_port, uint32_t _robot_count, uint16_t _remote_conveyor_port) : controller_server_(UA_Server_new()), controller_port_(_controller_port), running_(true), place_remove_finished_order_notification_(false) {
    /* Setup controller */
    UA_ServerConfig* controller_server_config = UA_Server_getConfig(controller_server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(controller_server_config, controller_port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the controller server");
        running_ = false;
        return;
    }

    receive_robot_state_inserter_.add_input_argument("robot port", "port", UA_TYPES_UINT16);
    receive_robot_state_inserter_.add_input_argument("robot busy status", "busy", UA_TYPES_BOOLEAN);
    receive_robot_state_inserter_.add_output_argument("robot state received", "robot_state_received", UA_TYPES_BOOLEAN);
    status = receive_robot_state_inserter_.add_method_node(controller_server_, UA_NODEID_STRING(1, RECEIVE_ROBOT_STATE), "receive robot state", receive_robot_state, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive robot state method node");
        running_ = false;
        return;
    }

    receive_conveyor_state_inserter_.add_input_argument("conveyor plate id", "plate_id", UA_TYPES_UINT32);
    receive_conveyor_state_inserter_.add_input_argument("conveyor plate busy status", "busy", UA_TYPES_BOOLEAN);
    receive_conveyor_state_inserter_.add_input_argument("conveyor plate position", "plate_position", UA_TYPES_UINT16);
    receive_conveyor_state_inserter_.add_output_argument("conveyor plate state received", "conveyor_state_received", UA_TYPES_BOOLEAN);
    status = receive_conveyor_state_inserter_.add_method_node(controller_server_, UA_NODEID_STRING(1, RECEIVE_CONVEYOR_STATE), "receive conveyor state", receive_conveyor_state, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive conveyor state method node");
        running_ = false;
        return;
    }

    receive_proceeded_to_next_tick_notification_inserter_.add_input_argument("remote port", "remote_port", UA_TYPES_UINT16);
    status = receive_proceeded_to_next_tick_notification_inserter_.add_method_node(controller_server_, UA_NODEID_STRING(1, RECEIVE_PROCEEDED_TO_NEXT_TICK_NOTIFICATION), "receive proceeded to next tick notification", receive_proceeded_to_next_tick_notification, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive proceeded to next tick notification");
        running_ = false;
        return;
    }

    status = place_remove_finished_order_notification_node_inserter_.add_information_node(controller_server_, UA_NODEID_STRING(1, PLACE_REMOVE_FINISHED_ORDER), "place remove finished order notifier", UA_TYPES_BOOLEAN, &place_remove_finished_order_notification_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error adding place_remove_finished_order_notification information node");
        running_ = false;
        return;
    }

    /* Setup conveyor client */
    remote_conveyor_ = std::make_unique<remote_conveyor>(_remote_conveyor_port);
    for (size_t i = 0; i < _robot_count+1; i++) {
        remote_plates_.push_back(remote_plate(i));
    }

    /* Setup robot clients */
    for (size_t i = 0; i < _robot_count; i++) {
        uint16_t remote_port = _robot_start_port + i;
        port_remote_robot_map_[remote_port] = std::make_unique<remote_robot>(remote_port);
    }

    /* Run the controller server */
    status = UA_Server_run_startup(controller_server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error at controller startup");
        running_ = false;
        return;
    }
    try {
        controller_server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_Server_run_iterate(controller_server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running controller");
        running_ = false;
        return;
    }
}

controller::~controller() {
    running_ = false;
    join_threads();
    UA_Server_run_shutdown(controller_server_);
    UA_Server_delete(controller_server_);
}

UA_StatusCode
controller::receive_robot_state(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad input size");
        return UA_STATUSCODE_BAD;
    }
    /* Extract input arguments */
    UA_UInt16 port = *(UA_UInt16*)_input[0].data;
    UA_Boolean busy = *(UA_Boolean*)_input[1].data;
    /* Extract method context */
    controller* self = static_cast<controller*>(_method_context);
    self->handle_robot_state(port, busy, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_robot_state(UA_UInt16 _port, UA_Boolean _busy, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(port_remote_robot_map_.find(_port) == port_remote_robot_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Robot with port %d not found", _port);
        return;
    }
    UA_Boolean robot_state_received = true;
    UA_Variant_setScalarCopy(_output, &robot_state_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    remote_robot& robot = port_remote_robot_map_[_port].operator*();
    robot.set_busy_status(_busy);
    received_robot_states_.insert(_port);
    if(received_robot_states_.size() == port_remote_robot_map_.size()) {
        received_robot_states_.clear();
        handle_all_robot_states_received();
    }
}

void
controller::handle_all_robot_states_received() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    for(auto& port_robot_pair : port_remote_robot_map_) {
        remote_robot& robot = port_robot_pair.second.operator*();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Robot with port %d has busy status %d", robot.get_port(), robot.is_busy());
        robot.instruct(robot.get_port(), receive_robot_task_called, this);
    }
}

void
controller::receive_robot_task_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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

    UA_Boolean robot_task_sent;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        robot_task_sent = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s Remote robot response for instruction: %d", __FUNCTION__, robot_task_sent);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    controller* self = static_cast<controller*>(_userdata);
    self->handle_receive_robot_task_called_result(robot_task_sent);
}

void
controller::handle_receive_robot_task_called_result(UA_Boolean _robot_task_sent) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    //TODO: Rethink if it is not more suitable to put robot as userdata
    if (!_robot_task_sent)
        return;
}

UA_StatusCode
controller::receive_conveyor_state(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Extract input arguments */
    if(_input_size != 3) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad input size");
        return UA_STATUSCODE_BAD;
    }

    UA_UInt32 plate_id = *(UA_UInt32*)_input[0].data;
    UA_Boolean busy = *(UA_Boolean*)_input[1].data;
    UA_UInt16 adjacent_robot_position = *(UA_UInt16*)_input[2].data;
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }

    controller* self = static_cast<controller*>(_method_context);
    self->handle_conveyor_state(plate_id, busy, adjacent_robot_position, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_conveyor_state(UA_UInt32 _plate_id, UA_Boolean _busy, UA_UInt16 _adjacent_robot_position, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_plate_id >= remote_plates_.size()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Invalid plate id %d. Remote plates count is %d", __FUNCTION__, _plate_id, remote_plates_.size());
        return;
    }
    UA_Boolean conveyor_state_received = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(_output, &conveyor_state_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output", __FUNCTION__);
    }
    remote_plate& plate = remote_plates_[_plate_id];
    plate.set_busy_state(_busy);
    plate.set_adjacent_robot_position(_adjacent_robot_position);
    received_conveyor_states_.insert(_plate_id);
    if(received_conveyor_states_.size() == remote_plates_.size()) {
        received_conveyor_states_.clear();
        handle_all_conveyor_states_received();
    }
}

void
controller::handle_all_conveyor_states_received() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for(auto& plate : remote_plates_) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Plate with id %d has currently adjacent robot at position %u", plate.get_id(), plate.get_adjacent_robot_position());
    }
    remote_conveyor_->instruct(1, receive_conveyor_instruction_called, this);
}

void
controller::receive_conveyor_instruction_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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

    UA_Boolean conveyor_move_instruction_received;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        conveyor_move_instruction_received = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s Remote conveyor response for instruction: %d", __FUNCTION__, conveyor_move_instruction_received);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    controller* self = static_cast<controller*>(_userdata);
    self->handle_receive_conveyor_instruction_called_result(conveyor_move_instruction_received);
}

void
controller::handle_receive_conveyor_instruction_called_result(UA_Boolean conveyor_move_instruction_received) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if (!conveyor_move_instruction_received)
        return;
}

UA_StatusCode
controller::receive_proceeded_to_next_tick_notification(UA_Server *_server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Extract input arguments */
    if(_input_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad input size");
        return UA_STATUSCODE_BAD;
    }
    UA_UInt16 remote_port = *(UA_UInt16*)_input[0].data;
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(_method_context);
    self->handle_proceeded_to_next_tick_notification(remote_port, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_proceeded_to_next_tick_notification(UA_UInt16 _port, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_Boolean proceeded_to_next_tick_notification_received = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(_output, &proceeded_to_next_tick_notification_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output", __FUNCTION__);
    }
    received_proceeded_to_next_tick_notifications_.insert(_port);
    if (received_proceeded_to_next_tick_notifications_.size() == (port_remote_robot_map_.size()+1)) {
        received_proceeded_to_next_tick_notifications_.clear();
        place_remove_finished_order_notification_ = true;
        UA_Variant place_remove_finished_order_variant;
        UA_Variant_setScalar(&place_remove_finished_order_variant, &place_remove_finished_order_notification_, &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_Server_writeValue(controller_server_, UA_NODEID_STRING(1, PLACE_REMOVE_FINISHED_ORDER), place_remove_finished_order_variant);
    }
}

void
controller::join_threads() {
    if (controller_server_iterate_thread_.joinable())
        controller_server_iterate_thread_.join();
}

void
controller::start() {
    if (!running_)
        return;
    join_threads();
}

void
controller::stop() {
    running_ = false;
}