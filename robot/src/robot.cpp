#include "../include/robot.hpp"
#include "node_ids.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>
#include <string>
#include "client_connection_establisher.hpp"

robot::robot(UA_UInt32 _robot_id, UA_UInt16 _robot_port, UA_UInt16 _controller_port) : robot_server_(UA_Server_new()), robot_id_(_robot_id), robot_port_(_robot_port), controller_client_(UA_Client_new()), running_(true), current_recipe_id_in_process_(0) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* robot_server_config = UA_Server_getConfig(robot_server_);
    status = UA_ServerConfig_setMinimal(robot_server_config, robot_port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "%s: Error with setting up the server", __FUNCTION__);
        running_ = false;
        return;
    }

    receive_task_inserter_.add_input_argument("recipe id", "recipe_id", UA_TYPES_UINT32);
    receive_task_inserter_.add_output_argument("task received", "task_received", UA_TYPES_BOOLEAN);
    status = receive_task_inserter_.add_method_node(robot_server_, UA_NODEID_STRING(1, RECEIVE_TASK), "receive robot task", receive_task, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the receive robot task method node", __FUNCTION__);
        running_ = false;
        return;
    }

    handover_finished_order_inserter_.add_output_argument("robot position", "robot_position", UA_TYPES_UINT32);
    handover_finished_order_inserter_.add_output_argument("recipe id", "recipe_id", UA_TYPES_UINT32);
    status = handover_finished_order_inserter_.add_method_node(robot_server_, UA_NODEID_STRING(1, HANDOVER_FINSIHED_ORDER), "handover finished order", handover_finished_order, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the handover finished order method node", __FUNCTION__);
        running_ = false;
        return;
    }

    /* Run the robot server */
    status = UA_Server_run_startup(robot_server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "%s: Error at robot startup", __FUNCTION__);
        running_ = false;
        return;
    }
    try {
        robot_server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_Server_run_iterate(robot_server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "%s: Error running robot", __FUNCTION__);
        running_ = false;
        return;
    }

    /* Setup controller client */
    client_connection_establisher controller_client_connection_establisher;
    UA_SessionState controller_session_state = controller_client_connection_establisher.establish_connection(controller_client_, _controller_port);
    if (controller_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing controller client session", __FUNCTION__);
        running_ = false;
        return;
    }

    if (controller_session_state == UA_SESSIONSTATE_ACTIVATED) {
        receive_robot_state_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);
        receive_robot_state_caller_.add_input_argument(&busy_status_, UA_TYPES_BOOLEAN);
        receive_robot_state_caller_.add_input_argument(&current_tool_, UA_TYPES_UINT32);

        controller_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(controller_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "%s: Error running the controller client", __FUNCTION__);
                    running_ = false;
                }
            }
        });
    }
}

void
robot::receive_robot_state_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Userdata is NULL", __FUNCTION__);
        return;
    }

    UA_StatusCode status_code = _response->responseHeader.serviceResult;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad service result", __FUNCTION__);
        return;
    }

    UA_Boolean robot_state_received;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        robot_state_received = *(UA_Boolean*)_response->results[0].outputArguments->data;
        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, robot_state_received);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }
    
    robot* self = static_cast<robot*>(_userdata);
    self->handle_robot_state_result(robot_state_received);
}

void
robot::handle_robot_state_result(UA_Boolean _robot_state_received) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_robot_state_received) {
        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s Controller received state successfully", __FUNCTION__);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Controller returned false", __FUNCTION__);
    }
}

UA_StatusCode
robot::receive_task(UA_Server *_server,
            const UA_NodeId *_session_id, void *_session_context,
            const UA_NodeId *_method_id, void *_method_context,
            const UA_NodeId *_object_id, void *_object_context,
            size_t _input_size, const UA_Variant *_input,
            size_t _output_size, UA_Variant *_output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    UA_UInt32 recipe_id = *(UA_UInt32*)_input[0].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    robot* self = static_cast<robot*>(_method_context);
    self->handle_receive_task(recipe_id, _output);
    return UA_STATUSCODE_GOOD;
}

void
robot::handle_receive_task(UA_UInt32 _recipe_id, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "INSTRUCTIONS: Received instruction(recipe_id=%d)", _recipe_id);
    // TODO cache the action queue
    busy_status_ = true;
    current_recipe_id_in_process_ = _recipe_id;

    UA_Boolean task_received = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(_output, &task_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error returning receive task ack", __FUNCTION__);
        running_ = false;
    }
}

UA_StatusCode
robot::handover_finished_order(UA_Server *_server,
        const UA_NodeId *_session_id, void *_session_context,
        const UA_NodeId *_method_id, void *_method_context,
        const UA_NodeId *_object_id, void *_object_context,
        size_t _input_size, const UA_Variant *_input,
        size_t _output_size, UA_Variant *_output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 0) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    robot* self = static_cast<robot*>(_method_context);
    self->handle_handover_finished_order(_output);
    return UA_STATUSCODE_GOOD;
}

void
robot::handle_handover_finished_order(UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    // UA_Variant_setScalarCopy(&_output[0], &tmp, &UA_TYPES[UA_TYPES_STRING]);
    // UA_Boolean output_two = false;
    // UA_Variant_setScalarCopy(&_output[1], &output_two, &UA_TYPES[UA_TYPES_BOOLEAN]);

}

robot::~robot() {
    running_ = false;
    join_threads();
    UA_Server_run_shutdown(robot_server_);
    UA_Server_delete(robot_server_);
    UA_Client_delete(controller_client_);
}

void
robot::join_threads() {
    if (robot_server_iterate_thread_.joinable())
        robot_server_iterate_thread_.join();
    if (controller_client_iterate_thread_.joinable())
        controller_client_iterate_thread_.join();
}

void
robot::start() {
    if (!running_)
        return;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "STATES: Send state(port=%d, busy_state=%d)", robot_port_, busy_status_);
    UA_StatusCode status = receive_robot_state_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, RECEIVE_ROBOT_STATE), receive_robot_state_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling the receive robot state method node", __FUNCTION__);
        running_ = false;
    }
    join_threads();
}

void
robot::stop() {
    running_ = false;
}