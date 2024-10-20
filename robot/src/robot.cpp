#include "../include/robot.hpp"
#include "node_ids.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>
#include <string>
#include "client_connection_establisher.hpp"

robot::robot(UA_UInt32 _robot_id, UA_UInt16 _robot_port, UA_UInt16 _clock_port, UA_UInt16 _controller_port, UA_UInt16 _conveyor_port) : robot_server_(UA_Server_new()), robot_id_(_robot_id), robot_port_(_robot_port), clock_client_(UA_Client_new()), controller_client_(UA_Client_new()), current_clock_tick_(0), next_clock_tick_(0), conveyor_client_(UA_Client_new()), running_(true) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* robot_server_config = UA_Server_getConfig(robot_server_);
    status = UA_ServerConfig_setMinimal(robot_server_config, robot_port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
    }

    /* Run the robot server */
    robot_server_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_StatusCode status = UA_Server_run_iterate(robot_server_, true);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running the robot server");
            }
        }
    });

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
        receive_tick_ack_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);
        receive_tick_ack_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
        receive_tick_ack_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);
    }

    /* Setup controller client */
    client_connection_establisher controller_client_connection_establisher;
    UA_SessionState controller_session_state = controller_client_connection_establisher.establish_connection(controller_client_, _controller_port);
    if (controller_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing controller client session");
    }

    if (controller_session_state == UA_SESSIONSTATE_ACTIVATED) {
        controller_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(controller_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running the controller client");
                }
            }
        });
    }

    receive_robot_state_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);
    receive_robot_state_caller_.add_input_argument(&busy_status_, UA_TYPES_BOOLEAN);
    receive_robot_state_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
    receive_robot_state_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);

    receive_task_inserter_.add_input_argument("activitiy id", "activity_id", UA_TYPES_UINT32);
    receive_task_inserter_.add_input_argument("ingredient id", "ingredient_id", UA_TYPES_UINT32);
    receive_task_inserter_.add_output_argument("task received", "task_received", UA_TYPES_BOOLEAN);
    status = receive_task_inserter_.add_method_node(robot_server_, UA_NODEID_STRING(1, RECEIVE_TASK), "receive robot task", receive_task, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive robot task method node");
    }

    /* Setup conveyor client */
    client_connection_establisher conveyor_client_connection_establisher;
    UA_SessionState conveyor_session_state = conveyor_client_connection_establisher.establish_connection(conveyor_client_, _conveyor_port);
    if (conveyor_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing conveyor client session");
    }

    if (conveyor_session_state == UA_SESSIONSTATE_ACTIVATED) {
        conveyor_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(conveyor_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running the conveyor client");
                }
            }
        });
    }

    place_finished_order_caller_.add_input_argument(&finished_order_id_, UA_TYPES_UINT32);
    receive_proceeded_to_next_tick_notification_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);
}

void
robot::clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                        UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if(UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) _value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);

        robot* self = static_cast<robot*>(_monitor_context);
        self->handle_clock_tick_notification(new_clock_tick);
    }
}

void
robot::handle_clock_tick_notification(UA_UInt64 _new_clock_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    current_clock_tick_ = _new_clock_tick;
    progress_new_tick(_new_clock_tick);
}

void
robot::receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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
    
    robot* self = static_cast<robot*>(_userdata);
    self->handle_receive_tick_ack_result(tick_ack_result);
}

void
robot::handle_receive_tick_ack_result(UA_Boolean _tick_ack_result) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
}

void
robot::receive_robot_state_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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

    UA_Boolean robot_state_received;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        robot_state_received = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, robot_state_received);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    robot* self = static_cast<robot*>(_userdata);
    self->handle_robot_state_result(robot_state_received);
}

void
robot::handle_robot_state_result(UA_Boolean _robot_state_received) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    if (!_robot_state_received)
        return;
}

UA_StatusCode
robot::receive_task(UA_Server *_server,
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
    UA_UInt32 recipe_id = *(UA_UInt32*)_input[0].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    robot* self = static_cast<robot*>(_method_context);
    self->handle_receive_task(recipe_id, _output);
    return UA_STATUSCODE_GOOD;
}

void
robot::handle_receive_task(UA_UInt32 _recipe_id, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    next_clock_tick_++;
    UA_StatusCode status = receive_tick_ack_caller_.call_method_node(clock_client_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
    }
    UA_Boolean task_received = true;
    status = UA_Variant_setScalarCopy(_output, &task_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error returning receive task ack");
    }
}

void
robot::place_finished_order_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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

    UA_Boolean place_finished_order_successful;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        place_finished_order_successful = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, place_finished_order_successful);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    robot* self = static_cast<robot*>(_userdata);
    self->handle_place_finished_order_result(place_finished_order_successful);
}

void
robot::handle_place_finished_order_result(UA_Boolean _place_finished_order_successful) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (busy_status_ && _place_finished_order_successful) {
        busy_status_ = false;
    } 
}

void
robot::receive_proceeded_to_next_tick_notification_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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

    UA_Boolean proceeded_to_next_tick_notification_received;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        proceeded_to_next_tick_notification_received = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, proceeded_to_next_tick_notification_received);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    robot* self = static_cast<robot*>(_userdata);
    self->handle_proceeded_to_next_tick_notification_result(proceeded_to_next_tick_notification_received);
}

void
robot::handle_proceeded_to_next_tick_notification_result(UA_Boolean _proceeded_to_next_tick_notification_received) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_proceeded_to_next_tick_notification_received) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s was successful", __FUNCTION__);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s failed", __FUNCTION__);
    }
}

void
robot::progress_new_tick(UA_UInt64 _new_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s with new tick %lu", __FUNCTION__, _new_tick);
    receive_proceeded_to_next_tick_notification_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, RECEIVE_PROCEEDED_TO_NEXT_TICK_NOTIFICATION), receive_proceeded_to_next_tick_notification_called, this);
    place_finished_order_caller_.call_method_node(conveyor_client_, UA_NODEID_STRING(1, PLACE_FINISHED_ORDER), place_finished_order_called, this);
}

robot::~robot() {
    UA_Server_delete(robot_server_);
    UA_Client_delete(clock_client_);
    UA_Client_delete(controller_client_);
}

void
robot::start() {
    next_clock_tick_++; // TODO: Compute from task instruction
    UA_StatusCode status = receive_robot_state_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, RECEIVE_ROBOT_STATE), receive_robot_state_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
        running_ = false;
    }

    clock_client_iterate_thread_.join();
    controller_client_iterate_thread_.join();
    robot_server_iterate_thread_.join();
    conveyor_client_iterate_thread_.join();
}

void
robot::stop() {
    running_ = false;
}