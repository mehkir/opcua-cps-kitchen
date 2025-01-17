#include "../include/robot.hpp"
#include "node_ids.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>
#include <string>
#include "client_connection_establisher.hpp"
#include "response_checker.hpp"
#include "callback_scheduler.hpp"
#include "information_node_inserter.hpp"
#include "information_node_writer.hpp"
#include "time_unit.hpp"

#define RECIPE_PATH "recipes.json"

robot::robot(position_t _position, port_t _port, port_t _controller_port, port_t _conveyor_port) :
        server_(UA_Server_new()), position_(_position), port_(_port), controller_client_(UA_Client_new()), conveyor_client_(UA_Client_new()), running_(true),
        state_(robot_state::IDLING), current_tool_(robot_tool::ROBOT_TOOLS_COUNT), recipe_id_in_process_(0), dish_in_process_("None"), action_in_process_("None"),
        ingredients_in_process_("None"), overall_time_(0), recipe_parser_(RECIPE_PATH) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    status = UA_ServerConfig_setMinimal(server_config, port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "%s: Error with setting up the server", __FUNCTION__);
        running_ = false;
        return;
    }

    receive_task_inserter_.add_input_argument("recipe id", "recipe_id", UA_TYPES_UINT32);
    receive_task_inserter_.add_output_argument("robot port", "robot_port", UA_TYPES_UINT16);
    receive_task_inserter_.add_output_argument("robot position", "robot_position", UA_TYPES_UINT32);
    receive_task_inserter_.add_output_argument("robot status", "robot_status", UA_TYPES_UINT32);
    status = receive_task_inserter_.add_method_node(server_, UA_NODEID_STRING(1, const_cast<char*>(RECEIVE_TASK)), "receive robot task", receive_task, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the receive robot task method node", __FUNCTION__);
        running_ = false;
        return;
    }

    handover_finished_order_inserter_.add_output_argument("robot port", "robot_port", UA_TYPES_UINT16);
    handover_finished_order_inserter_.add_output_argument("robot position", "robot_position", UA_TYPES_UINT32);
    handover_finished_order_inserter_.add_output_argument("recipe id", "recipe_id", UA_TYPES_UINT32);
    status = handover_finished_order_inserter_.add_method_node(server_, UA_NODEID_STRING(1, const_cast<char*>(HANDOVER_FINISHED_ORDER)), "handover finished order", handover_finished_order, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the handover finished order method node", __FUNCTION__);
        running_ = false;
        return;
    }

    information_node_inserter robot_state_information_node;
    status = robot_state_information_node.add_information_node(server_, UA_NODEID_STRING(1, const_cast<char*>(ROBOT_STATE)), "robot state", UA_TYPES_UINT32, &state_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the robot state information node", __FUNCTION__);
        running_ = false;
        return;
    }

    information_node_inserter recipe_id_information_node;
    status = recipe_id_information_node.add_information_node(server_, UA_NODEID_STRING(1, const_cast<char*>(RECIPE_ID)), "recipe id", UA_TYPES_UINT32, &recipe_id_in_process_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the recipe id information node", __FUNCTION__);
        running_ = false;
        return;
    }

    information_node_inserter dish_name_information_node;
    UA_String dish_in_process = UA_STRING(const_cast<char*>(dish_in_process_.c_str()));
    status = dish_name_information_node.add_information_node(server_, UA_NODEID_STRING(1, const_cast<char*>(DISH_NAME)), "dish name", UA_TYPES_STRING, &dish_in_process);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the dish name information node", __FUNCTION__);
        running_ = false;
        return;
    }

    UA_String action_in_process = UA_STRING(const_cast<char*>(action_in_process_.c_str()));
    information_node_inserter action_name_information_node;
    status = action_name_information_node.add_information_node(server_, UA_NODEID_STRING(1, const_cast<char*>(ACTION_NAME)), "action name", UA_TYPES_STRING, &action_in_process);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the action name information node", __FUNCTION__);
        running_ = false;
        return;
    }

    UA_String ingredients_in_process = UA_STRING(const_cast<char*>(ingredients_in_process_.c_str()));
    information_node_inserter ingredients_information_node;
    status = ingredients_information_node.add_information_node(server_, UA_NODEID_STRING(1, const_cast<char*>(INGREDIENTS)), "ingredients", UA_TYPES_STRING, &ingredients_in_process);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the ingredients information node", __FUNCTION__);
        running_ = false;
        return;
    }

    information_node_inserter overall_time_information_node;
    status = overall_time_information_node.add_information_node(server_, UA_NODEID_STRING(1, const_cast<char*>(OVERALL_TIME)), "overall time", UA_TYPES_UINT32, &overall_time_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the overall time information node", __FUNCTION__);
        running_ = false;
        return;
    }

    /* Run the robot server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "%s: Error at robot startup", __FUNCTION__);
        running_ = false;
        return;
    }
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_Server_run_iterate(server_, true);
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
        receive_robot_state_caller_.add_input_argument(&port_, UA_TYPES_UINT16);
        receive_robot_state_caller_.add_input_argument(&position_, UA_TYPES_UINT32);
        receive_robot_state_caller_.add_input_argument(&state_, UA_TYPES_UINT32);
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

    /* Setup conveyor client */
    client_connection_establisher conveyor_client_connection_establisher;
    UA_SessionState conveyor_session_state = conveyor_client_connection_establisher.establish_connection(conveyor_client_, _conveyor_port);
    if (conveyor_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing conveyor client session", __FUNCTION__);
        running_ = false;
        return;
    }

    if (conveyor_session_state == UA_SESSIONSTATE_ACTIVATED) {
        receive_finished_order_notification_caller_.add_input_argument(&port_, UA_TYPES_UINT16);
        receive_finished_order_notification_caller_.add_input_argument(&position_, UA_TYPES_UINT32);

        conveyor_client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Client_run_iterate(conveyor_client_, 100);
                if(status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "%s: Error running the conveyor client", __FUNCTION__);
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
    response_checker response(_response);
    UA_StatusCode status_code = response.get_service_result();;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad service result", __FUNCTION__);
        return;
    }
    if(!response.has_scalar_type(0, 0, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }   

    UA_Boolean robot_state_received = *(UA_Boolean*)response.get_data(0,0);
    
    robot* self = static_cast<robot*>(_userdata);
    self->handle_robot_state_result(robot_state_received);
}

void
robot::handle_robot_state_result(UA_Boolean _robot_state_received) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (!_robot_state_received) {
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

    if (!UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    recipe_id_t recipe_id = *(recipe_id_t*)_input[0].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    robot* self = static_cast<robot*>(_method_context);
    self->handle_receive_task(recipe_id, _output);
    return UA_STATUSCODE_GOOD;
}

void
robot::handle_receive_task(recipe_id_t _recipe_id, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "INSTRUCTIONS: Received instruction(recipe_id=%d)", _recipe_id);
    if (state_ != robot_state::IDLING) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot is still busy", __FUNCTION__);
        return;
    }

    if (!recipe_parser_.has_recipe(_recipe_id)) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Unknown recipe ID", __FUNCTION__);
        return;
    }

    state_ = robot_state::COOKING;
    information_node_writer robot_state_writer;
    UA_StatusCode status = robot_state_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(ROBOT_STATE)), &state_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot state write failed", __FUNCTION__);
        running_ = false;
    }
    recipe_id_in_process_ = _recipe_id;
    information_node_writer recipe_id_writer;
    status = recipe_id_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(RECIPE_ID)), &recipe_id_in_process_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Recipe id write failed", __FUNCTION__);
        running_ = false;
    }

    status = UA_Variant_setScalarCopy(&_output[0], &port_, &UA_TYPES[UA_TYPES_UINT16]);
    status |= UA_Variant_setScalarCopy(&_output[1], &position_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[2], &state_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error returning states", __FUNCTION__);
        running_ = false;
    }

    recipe current_recipe = recipe_parser_.get_recipe(recipe_id_in_process_);
    dish_in_process_ = current_recipe.get_dish_name();
    UA_String dish_in_process = UA_STRING(const_cast<char*>(dish_in_process_.c_str()));
    information_node_writer dish_name_writer;
    status = dish_name_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(DISH_NAME)), &dish_in_process, &UA_TYPES[UA_TYPES_STRING]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Dish name write failed", __FUNCTION__);
        running_ = false;
    }
    action_queue_ = current_recipe.get_action_queue();
    overall_time_ = current_recipe.get_overall_time();
    overall_time_ += current_tool_ != action_queue_.front().get_required_tool() ? RETOOLING_TIME : 0;
    information_node_writer overall_time_writer;
    status = overall_time_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(OVERALL_TIME)), &overall_time_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Overall time write failed", __FUNCTION__);
        running_ = false;
    }
    determine_next_action();
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
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &port_, &UA_TYPES[UA_TYPES_UINT16]);
    status |= UA_Variant_setScalarCopy(&_output[1], &position_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[2], &recipe_id_in_process_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error during handover finished order", __FUNCTION__);
        running_ = false;
        return;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "HANDOVER: Pass finished recipe_id=%d (port=%d, position=%d)", recipe_id_in_process_, port_, position_);
    recipe_id_in_process_ = 0;
    information_node_writer recipe_id_writer;
    status = recipe_id_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(RECIPE_ID)), &recipe_id_in_process_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Recipe id write failed", __FUNCTION__);
        running_ = false;
    }
    state_ = robot_state::IDLING;
    information_node_writer robot_state_writer;
    status = robot_state_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(ROBOT_STATE)), &state_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot state write failed", __FUNCTION__);
        running_ = false;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "STATES: Send state(port=%d, robot_state=%s)", port_, robot_state_to_string(state_));
    status = receive_robot_state_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, const_cast<char*>(RECEIVE_ROBOT_STATE)), receive_robot_state_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed sending robot state to controller", __FUNCTION__);
        running_ = false;
    }
}

robot::~robot() {
    running_ = false;
    join_threads();
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    UA_Client_delete(controller_client_);
}

void
robot::determine_next_action() {
    if (action_queue_.size()) {
        robot_action robot_act = action_queue_.front();
        robot_tool required_tool = robot_act.get_required_tool();
        if (required_tool != current_tool_) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "RETOOL: Retooling current tool %s to %s", robot_tool_to_string(current_tool_), robot_tool_to_string(required_tool));
            callback_scheduler retool_scheduler(server_, retool, this, NULL);
            UA_StatusCode status = retool_scheduler.schedule_from_now(UA_DateTime_nowMonotonic() + (RETOOLING_TIME * TIME_UNIT));
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling retooling", __FUNCTION__);
                running_ = false;
            }
        } else {
            action_in_process_ = robot_act.get_name();
            UA_String action_in_process = UA_STRING(const_cast<char*>(action_in_process_.c_str()));
            information_node_writer action_name_writer;
            UA_StatusCode status = action_name_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(ACTION_NAME)), &action_in_process, &UA_TYPES[UA_TYPES_STRING]);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Action name write failed", __FUNCTION__);
                running_ = false;
            }
            ingredients_in_process_ = robot_act.get_ingredients();
            UA_String ingredients_in_process = UA_STRING(const_cast<char*>(ingredients_in_process_.c_str()));
            information_node_writer ingredients_writer;
            status = ingredients_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(INGREDIENTS)), &ingredients_in_process, &UA_TYPES[UA_TYPES_STRING]);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Ingredients write failed", __FUNCTION__);
                running_ = false;
            }
            duration_t action_duration = robot_act.get_action_duration();
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "COOK: Performing %s on recipe_id=%d with ingredients=%s for %lld", robot_act.get_name().c_str(), recipe_id_in_process_, robot_act.get_ingredients().c_str(), action_duration);
            callback_scheduler action_scheduler(server_, perform_action, this, NULL);
            status = action_scheduler.schedule_from_now(UA_DateTime_nowMonotonic() + (action_duration * TIME_UNIT));
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling perform action", __FUNCTION__);
                running_ = false;
            }
        }
    } else {
        state_ = robot_state::FINISHED;
        information_node_writer robot_state_writer;
        UA_StatusCode status = robot_state_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(ROBOT_STATE)), &state_, &UA_TYPES[UA_TYPES_UINT32]);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot state write failed", __FUNCTION__);
            running_ = false;
        }
        dish_in_process_ = "None";
        UA_String dish_in_process = UA_STRING(const_cast<char*>(dish_in_process_.c_str()));
        information_node_writer dish_name_writer;
        status = dish_name_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(DISH_NAME)), &dish_in_process, &UA_TYPES[UA_TYPES_STRING]);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Dish name write failed", __FUNCTION__);
            running_ = false;
        }
        action_in_process_ = "None";
        UA_String action_in_process = UA_STRING(const_cast<char*>(action_in_process_.c_str()));
        information_node_writer action_name_writer;
        status = action_name_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(ACTION_NAME)), &action_in_process, &UA_TYPES[UA_TYPES_STRING]);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Action name write failed", __FUNCTION__);
            running_ = false;
        }
        ingredients_in_process_ = "None";
        UA_String ingredients_in_process = UA_STRING(const_cast<char*>(ingredients_in_process_.c_str()));
        information_node_writer ingredients_writer;
        status = ingredients_writer.write_value(server_, UA_NODEID_STRING(1, const_cast<char*>(INGREDIENTS)), &ingredients_in_process, &UA_TYPES[UA_TYPES_STRING]);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Ingredients write failed", __FUNCTION__);
            running_ = false;
        }

        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "COOK: Recipe_id=%d finished, send finished order notification", recipe_id_in_process_);
        status = receive_finished_order_notification_caller_.call_method_node(conveyor_client_, UA_NODEID_STRING(1, const_cast<char*>(FINISHED_ORDER_NOTIFICATION)), receive_finished_order_notification_called, this);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to send finished order notification", __FUNCTION__);
            running_ = false;
        }
    }
}

void
robot::receive_finished_order_notification_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Userdata is NULL", __FUNCTION__);
        return;
    }
    response_checker response(_response);
    UA_StatusCode status_code = response.get_service_result();;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad service result", __FUNCTION__);
        return;
    }

    UA_Boolean finished_order_notification_received = false;
    if(!response.has_scalar_type(0, 0, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }
    finished_order_notification_received = *(UA_Boolean*)response.get_data(0,0);
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, finished_order_notification_received);
    
    robot* self = static_cast<robot*>(_userdata);
    self->handle_finished_order_notification_result(finished_order_notification_received);
}

void
robot::handle_finished_order_notification_result(UA_Boolean _finished_order_notification_received) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_finished_order_notification_received) {
        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Conveyor received state successfully", __FUNCTION__);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Conveyor returned false", __FUNCTION__);
    }
}

void
robot::perform_action(UA_Server* _server, void* _data) {
    if (_data == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Data is NULL", __FUNCTION__);
        return;
    }
    robot* self = static_cast<robot*>(_data);
    robot_action robot_act = self->action_queue_.front();
    duration_t action_duration = robot_act.get_action_duration();
    self->overall_time_ -= action_duration;
    information_node_writer overall_time_writer;
    UA_StatusCode status = overall_time_writer.write_value(_server, UA_NODEID_STRING(1, const_cast<char*>(OVERALL_TIME)), &self->overall_time_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Overall time write failed", __FUNCTION__);
        self->running_ = false;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "COOK: Performed %s on recipe_id=%d with ingredients=%s for %lld", robot_act.get_name().c_str(), self->recipe_id_in_process_, robot_act.get_ingredients().c_str(), action_duration);
    self->action_queue_.pop();
    self->determine_next_action();
}

void
robot::retool(UA_Server* _server, void* _data) {
    if (_data == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Data is NULL", __FUNCTION__);
        return;
    }
    robot* self = static_cast<robot*>(_data);
    self->current_tool_ = self->action_queue_.front().get_required_tool();
    self->overall_time_ -= RETOOLING_TIME;
    information_node_writer overall_time_writer;
    UA_StatusCode status = overall_time_writer.write_value(_server, UA_NODEID_STRING(1, const_cast<char*>(OVERALL_TIME)), &self->overall_time_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Overall time write failed", __FUNCTION__);
        self->running_ = false;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "RETOOL: Current tool now is %s", robot_tool_to_string(self->current_tool_));
    self->determine_next_action();
}

void
robot::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if (controller_client_iterate_thread_.joinable())
        controller_client_iterate_thread_.join();
    if (conveyor_client_iterate_thread_.joinable())
        conveyor_client_iterate_thread_.join();
}

const char*
robot::robot_state_to_string(robot_state _state) {
    switch (_state) {
    case robot_state::IDLING: return "IDLING";
    case robot_state::COOKING: return "COOKING";
    case robot_state::FINISHED: return "FINISHED";
    default: return "Unimplemented item";
    }
}

void
robot::start() {
    if (!running_)
        return;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "STATES: Send state(port=%d, robot_state=%s)", port_, robot_state_to_string(state_));
    UA_StatusCode status = receive_robot_state_caller_.call_method_node(controller_client_, UA_NODEID_STRING(1, const_cast<char*>(RECEIVE_ROBOT_STATE)), receive_robot_state_called, this);
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