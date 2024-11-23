#include "../include/robot.hpp"
#include "node_ids.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>
#include <string>
#include "client_connection_establisher.hpp"

robot::robot(UA_UInt32 _robot_id, UA_UInt16 _robot_port, UA_UInt16 _clock_port, UA_UInt16 _controller_port, UA_UInt16 _conveyor_port) : robot_server_(UA_Server_new()), robot_id_(_robot_id), robot_port_(_robot_port), clock_client_(UA_Client_new()), controller_client_(UA_Client_new()), current_clock_tick_(0), next_clock_tick_(0), conveyor_client_(UA_Client_new()), running_(true), finished_order_id_(0) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* robot_server_config = UA_Server_getConfig(robot_server_);
    status = UA_ServerConfig_setMinimal(robot_server_config, robot_port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        running_ = false;
        return;
    }

    receive_task_inserter_.add_input_argument("recipe id", "recipe_id", UA_TYPES_UINT32);
    receive_task_inserter_.add_output_argument("task received", "task_received", UA_TYPES_BOOLEAN);
    status = receive_task_inserter_.add_method_node(robot_server_, UA_NODEID_STRING(1, RECEIVE_TASK), "receive robot task", receive_task, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive robot task method node");
        running_ = false;
        return;
    }

    /* Run the robot server */
    status = UA_Server_run_startup(robot_server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error at robot startup");
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
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running robot");
        running_ = false;
        return;
    }

    /* Setup controller client */
    client_connection_establisher controller_client_connection_establisher;
    UA_SessionState controller_session_state = controller_client_connection_establisher.establish_connection(controller_client_, _controller_port);
    if (controller_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing controller client session");
        running_ = false;
        return;
    }

    if (controller_session_state == UA_SESSIONSTATE_ACTIVATED) {
        controller_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(controller_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running the controller client");
                    running_ = false;
                }
            }
        });
        receive_robot_state_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);
        receive_robot_state_caller_.add_input_argument(&busy_status_, UA_TYPES_BOOLEAN);

        receive_proceeded_to_next_tick_notification_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);

        status = place_remove_finished_order_notification_subscriber_.subscribe_node_value(controller_client_, UA_NODEID_STRING(1, PLACE_REMOVE_FINISHED_ORDER), place_remove_finished_order_notification_callback, this);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error subscribing to the place remove finished order notification node");
            running_ = false;
        }
    }

    /* Setup conveyor client */
    client_connection_establisher conveyor_client_connection_establisher;
    UA_SessionState conveyor_session_state = conveyor_client_connection_establisher.establish_connection(conveyor_client_, _conveyor_port);
    if (conveyor_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing conveyor client session");
        running_ = false;
        return;
    }

    if (conveyor_session_state == UA_SESSIONSTATE_ACTIVATED) {
        conveyor_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(conveyor_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running the conveyor client");
                    running_ = false;
                }
            }
        });
        place_finished_order_caller_.add_input_argument(&finished_order_id_, UA_TYPES_UINT32);
    }

    /* Setup clock client */
    client_connection_establisher clock_client_connection_establisher;
    UA_SessionState clock_session_state = clock_client_connection_establisher.establish_connection(clock_client_, _clock_port);
    if (clock_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing clock client session");
        running_ = false;
        return;
    }

    if (clock_session_state == UA_SESSIONSTATE_ACTIVATED) {
        /* Run the clock client */
        clock_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(clock_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running the clock client");
                    running_ = false;
                }
            }
        });

        /* Setup clock tick monitoring */
        status = clock_tick_subscriber_.subscribe_node_value(clock_client_, UA_NODEID_STRING(1, CLOCK_TICK), clock_tick_notification_callback, this);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error subscribing to the clock tick node");
            running_ = false;
        }

        /* Add receive tick ack method caller to send next tick */
        receive_tick_ack_caller_.add_input_argument(&robot_port_, UA_TYPES_UINT16);
        receive_tick_ack_caller_.add_input_argument(&current_clock_tick_, UA_TYPES_UINT64);
        receive_tick_ack_caller_.add_input_argument(&next_clock_tick_, UA_TYPES_UINT64);
    }
}

void
robot::clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                        UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) _value->value.data;
        if (new_clock_tick == 0) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Ignore initial zero notification", __FUNCTION__);
            return;
        }
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);

        robot* self = static_cast<robot*>(_monitor_context);
        self->handle_clock_tick_notification(new_clock_tick);
    }
}

void
robot::handle_clock_tick_notification(UA_UInt64 _new_clock_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_tick_ack_result) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s next tick transmission successful", __FUNCTION__);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s next tick transmission failed", __FUNCTION__);
    }
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_robot_state_received) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s Controller received state successfully", __FUNCTION__);
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: robot with port %d received recipe id %d", __FUNCTION__, robot_port_, _recipe_id);
    next_clock_tick_++; // TODO implement next clock tick according to next action, cache the action queue
    busy_status_ = true;
    finished_order_id_ = _recipe_id;
    UA_StatusCode status = receive_tick_ack_caller_.call_method_node(clock_client_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling the method node");
        running_ = false;
    }
    UA_Boolean task_received = true;
    status = UA_Variant_setScalarCopy(_output, &task_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error returning receive task ack");
        running_ = false;
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
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Plate occupation result is %d", __FUNCTION__, place_finished_order_successful);
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
        finished_order_id_ = 0;
    }
    UA_StatusCode status = receive_robot_state_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, RECEIVE_ROBOT_STATE), receive_robot_state_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling the receive robot state method node", __FUNCTION__);
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
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Reponse result is %d", __FUNCTION__, proceeded_to_next_tick_notification_received);
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
robot::place_remove_finished_order_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                                        UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_Boolean place_remove_finished_order = *(UA_Boolean *) _value->value.data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "Place remove finished oder result is: %d", place_remove_finished_order);

        robot* self = static_cast<robot*>(_monitor_context);
        self->handle_place_remove_finished_order_notification(place_remove_finished_order);
    }
}

void
robot::handle_place_remove_finished_order_notification(UA_Boolean _place_remove_finished_order) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_place_remove_finished_order) {
        // TODO check if recipe is done and then call place order
        UA_StatusCode status = place_finished_order_caller_.call_method_node(conveyor_client_, UA_NODEID_STRING(1, PLACE_FINISHED_ORDER), place_finished_order_called, this);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling the place finished order method node", __FUNCTION__);
            running_ = false;
        }
    }
}

void
robot::progress_new_tick(UA_UInt64 _new_tick) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s with new tick %lu", __FUNCTION__, _new_tick);
    // TODO implement proceeding using cached action queue
    UA_StatusCode status = receive_proceeded_to_next_tick_notification_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, RECEIVE_PROCEEDED_TO_NEXT_TICK_NOTIFICATION), receive_proceeded_to_next_tick_notification_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling the receive proceeded to next tick method node", __FUNCTION__);
    }
}

robot::~robot() {
    running_ = false;
    join_threads();
    UA_Server_run_shutdown(robot_server_);
    UA_Server_delete(robot_server_);
    UA_Client_delete(clock_client_);
    UA_Client_delete(controller_client_);
}

void
robot::join_threads() {
    if (robot_server_iterate_thread_.joinable())
        robot_server_iterate_thread_.join();
    if (controller_client_iterate_thread_.joinable())
        controller_client_iterate_thread_.join();
    if (conveyor_client_iterate_thread_.joinable())
        conveyor_client_iterate_thread_.join();
    if (clock_client_iterate_thread_.joinable())
        clock_client_iterate_thread_.join();
}

void
robot::start() {
    if (!running_)
        return;
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