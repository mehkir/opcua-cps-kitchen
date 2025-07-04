#include "../include/robot.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>
#include <string>
#include "client_connection_establisher.hpp"
#include "response_checker.hpp"
#include "callback_scheduler.hpp"
#include "time_unit.hpp"
#include "filtered_logger.hpp"
#include "browsenames.h"
#include "node_browser_helper.hpp"

#define INSTANCE_NAME "KitchenRobot"
#define RECIPE_PATH "recipes.json"
#define CAPABILITIES_PATH "./capabilities/"

robot::robot(position_t _position, port_t _port, port_t _controller_port, port_t _conveyor_port) :
        server_(UA_Server_new()), position_(_position), port_(_port), robot_type_inserter_(server_, ROBOT_TYPE), controller_client_(UA_Client_new()), conveyor_client_(UA_Client_new()), running_(true),
        current_tool_(robot_tool::ROBOT_TOOLS_COUNT), processed_steps_of_recipe_id_in_process_(0), next_suitable_robot_port_for_recipe_id_in_process_(0), next_suitable_robot_position_for_recipe_id_in_process_(0),
        current_action_duration_(0), recipe_parser_(RECIPE_PATH), capability_parser_(CAPABILITIES_PATH, _position) {
    /* Setup robot */
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    status = UA_ServerConfig_setMinimal(server_config, port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error with setting up the server", __FUNCTION__);
        running_ = false;
        return;
    }
    // *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add attributes */
    robot_type_inserter_.add_attribute(ROBOT_TYPE, RECIPE_ID);
    robot_type_inserter_.add_attribute(ROBOT_TYPE, DISH_NAME);
    robot_type_inserter_.add_attribute(ROBOT_TYPE, ACTION_NAME);
    robot_type_inserter_.add_attribute(ROBOT_TYPE, INGREDIENTS);
    robot_type_inserter_.add_attribute(ROBOT_TYPE, OVERALL_TIME);
    robot_type_inserter_.add_attribute(ROBOT_TYPE, CURRENT_TOOL);
    robot_type_inserter_.add_attribute(ROBOT_TYPE, LAST_EQUIPPED_TOOL);
    robot_type_inserter_.add_attribute(ROBOT_TYPE, CAPABILITIES);
    /* Add receive task method node */
    method_arguments receive_task_method_arguments;
    receive_task_method_arguments.add_input_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    receive_task_method_arguments.add_input_argument("the processed steps", "processed_steps", UA_TYPES_UINT32);
    receive_task_method_arguments.add_output_argument("the robot port", "robot_port", UA_TYPES_UINT16);
    receive_task_method_arguments.add_output_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    receive_task_method_arguments.add_output_argument("the result", "result", UA_TYPES_BOOLEAN);
    status = robot_type_inserter_.add_method(ROBOT_TYPE, RECEIVE_TASK, receive_task, receive_task_method_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, RECEIVE_TASK);
        running_ = false;
        return;
    }
    /* Add handover finished order method node */
    method_arguments handover_finished_order_method_arguments;
    handover_finished_order_method_arguments.add_output_argument("the robot port", "robot_port", UA_TYPES_UINT16);
    handover_finished_order_method_arguments.add_output_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    handover_finished_order_method_arguments.add_output_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    handover_finished_order_method_arguments.add_output_argument("the processed steps", "processed_steps", UA_TYPES_UINT32);
    handover_finished_order_method_arguments.add_output_argument("the target port", "target_port", UA_TYPES_UINT16);
    handover_finished_order_method_arguments.add_output_argument("the target position", "target_position", UA_TYPES_UINT32);
    status = robot_type_inserter_.add_method(ROBOT_TYPE, HANDOVER_FINISHED_ORDER, handover_finished_order, handover_finished_order_method_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, HANDOVER_FINISHED_ORDER);
        running_ = false;
        return;
    }
    /* Add robot type constructor */
    robot_type_inserter_.add_object_type_constructor(server_, robot_type_inserter_.get_object_type_id(ROBOT_TYPE));
    /* Instantiate robot type */
    robot_type_inserter_.add_object_instance(INSTANCE_NAME, ROBOT_TYPE);
    /* Set attribute values */
    /* Set recipe id in process */
    UA_UInt32 recipe_id_in_process = 0;
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, RECIPE_ID, &recipe_id_in_process, UA_TYPES_UINT32);
    /* Set dish name in process */
    UA_String dish_in_process = UA_STRING(const_cast<char*>("None"));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, DISH_NAME, &dish_in_process, UA_TYPES_STRING);
    /* Set action in process */
    UA_String action_in_process = UA_STRING(const_cast<char*>("None"));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, ACTION_NAME, &action_in_process, UA_TYPES_STRING);
    /* Set ingredients in process*/
    UA_String ingredients_in_process = UA_STRING(const_cast<char*>("None"));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, INGREDIENTS, &ingredients_in_process, UA_TYPES_STRING);
    /* Set overall time */
    UA_UInt32 overall_time = 0;
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, OVERALL_TIME, &overall_time, UA_TYPES_UINT32);
    /* Set current tool */
    UA_String current_tool = UA_STRING(const_cast<char*>(robot_tool_to_string(current_tool_)));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, CURRENT_TOOL, &current_tool, UA_TYPES_STRING);
    /* Set last equipped tool */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, LAST_EQUIPPED_TOOL, &current_tool_, UA_TYPES_UINT32);
    /* Set capabilities */
    std::unordered_set<std::string> capabilities = capability_parser_.get_capabilities();
    UA_String ua_capabilities[capabilities.size()];
    int i = 0;
    for (std::string capability : capabilities) {
        ua_capabilities[i] = UA_STRING(const_cast<char*>(capability.c_str()));
        i++;
    }
    robot_type_inserter_.set_array_attribute(INSTANCE_NAME, CAPABILITIES, ua_capabilities, capabilities.size(), UA_TYPES_STRING);
    /* Run the robot server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at robot startup", __FUNCTION__);
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
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot", __FUNCTION__);
        running_ = false;
        return;
    }
    /* Setup controller client */
    client_connection_establisher controller_client_connection_establisher(controller_client_);
    bool connected = controller_client_connection_establisher.establish_connection("opc.tcp://localhost:" + std::to_string(_controller_port));
    if (!connected) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing controller client session", __FUNCTION__);
        running_ = false;
        return;
    }
    controller_client_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_StatusCode status = UA_Client_run_iterate(controller_client_, 100);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running the controller client", __FUNCTION__);
                running_ = false;
            }
        }
    });

    /* Setup conveyor client */
    client_connection_establisher conveyor_client_connection_establisher(conveyor_client_);
    connected = conveyor_client_connection_establisher.establish_connection("opc.tcp://localhost:" + std::to_string(_conveyor_port));
    if (!connected) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing conveyor client session", __FUNCTION__);
        running_ = false;
        return;
    }
    conveyor_client_iterate_thread_ = std::thread([this]() {
        while(running_) {
            UA_StatusCode status = UA_Client_run_iterate(conveyor_client_, 100);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running the conveyor client", __FUNCTION__);
                running_ = false;
            }
        }
    });
}

void
robot::register_robot_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Userdata is NULL", __FUNCTION__);
        return;
    }
    response_checker response(_response);
    UA_StatusCode status_code = response.get_service_result();
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad service result", __FUNCTION__);
        return;
    }
    if(response.get_output_arguments_size(0) != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return;
    }
    if(!response.has_scalar_type(0, 0, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }
    UA_Boolean register_robot_received = *(UA_Boolean*)response.get_data(0,0);
    robot* self = static_cast<robot*>(_userdata);
    self->handle_register_robot_result(register_robot_received);
}

void
robot::handle_register_robot_result(UA_Boolean _register_robot_received) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (!_register_robot_received) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Controller returned false", __FUNCTION__);
        return;
    }
}

void
robot::choose_next_robot_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Userdata is NULL", __FUNCTION__);
        return;
    }
    response_checker response(_response);
    UA_StatusCode status_code = response.get_service_result();
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad service result", __FUNCTION__);
        return;
    }
    if(response.get_output_arguments_size(0) != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return;
    }
    if( !response.has_scalar_type(0, 0, &UA_TYPES[UA_TYPES_UINT16]) ||
        !response.has_scalar_type(0, 1, &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }
    port_t target_port = *(port_t*)response.get_data(0,0);
    position_t target_position = *(position_t*)response.get_data(0,1);
    robot* self = static_cast<robot*>(_userdata);
    self->handle_choose_next_robot_result(target_port, target_position);
}

void
robot::handle_choose_next_robot_result(port_t _target_port, position_t _target_position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_target_port == 0 || _target_position == 0) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: No suitable robot for next steps received", __FUNCTION__);
        running_ = false;
        return;
    }
    next_suitable_robot_port_for_recipe_id_in_process_ = _target_port;
    next_suitable_robot_position_for_recipe_id_in_process_ = _target_position;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Controller returned robot at position %d with port %d", next_suitable_robot_position_for_recipe_id_in_process_, next_suitable_robot_port_for_recipe_id_in_process_);
    /* Notify conveyor about finished order */
    method_node_caller receive_finished_order_notification_caller;
    receive_finished_order_notification_caller.add_scalar_input_argument(&port_, UA_TYPES_UINT16);
    receive_finished_order_notification_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
    UA_ClientConfig* conveyor_config = UA_Client_getConfig(conveyor_client_);
    std::string conveyor_endpoint((char*) conveyor_config->endpointUrl.data, conveyor_config->endpointUrl.length);
    object_method_info omi = node_browser_helper().get_method_id(conveyor_endpoint, CONVEYOR_TYPE, FINISHED_ORDER_NOTIFICATION);
    if (omi == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, FINISHED_ORDER_NOTIFICATION);
        running_ = false;
        return;        
    }
    UA_StatusCode status = receive_finished_order_notification_caller.call_method_node(conveyor_client_, omi.object_id_, omi.method_id_, receive_finished_order_notification_called, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to send finished order notification", __FUNCTION__);
        running_ = false;
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
    if(_input_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    UA_StatusCode status = !UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32]);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    recipe_id_t recipe_id = *(recipe_id_t*)_input[0].data;
    UA_UInt32 processed_steps = *(UA_UInt32*)_input[1].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    robot* self = static_cast<robot*>(_method_context);
    self->handle_receive_task(recipe_id, processed_steps, _output);
    return UA_STATUSCODE_GOOD;
}

void
robot::handle_receive_task(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "INSTRUCTIONS: Received instruction to cook recipe_id=%d with already %d processed steps", _recipe_id, _processed_steps);
    if (!recipe_parser_.has_recipe(_recipe_id)) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Unknown recipe ID", __FUNCTION__);
        return;
    }
    recipe incoming_recipe = recipe_parser_.get_recipe(_recipe_id);
    std::queue<robot_action> action_queue = incoming_recipe.get_action_queue();
    // Remove already processed steps
    for (size_t i = 0; i < _processed_steps; i++) {
        action_queue.pop();
    }
    compute_overall_time_and_determine_last_tool(action_queue);
    // Setup incoming order
    order_queue_.push(order(_recipe_id, _processed_steps, action_queue));
    // Set output parameters
    UA_Boolean task_received = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &port_, &UA_TYPES[UA_TYPES_UINT16]);
    status |= UA_Variant_setScalarCopy(&_output[1], &position_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[2], &task_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error returning states", __FUNCTION__);
        running_ = false;
    }
    /* Get recipe id in process */
    UA_Variant recipe_id_in_process_var;
    robot_type_inserter_.get_attribute(INSTANCE_NAME, RECIPE_ID, recipe_id_in_process_var);
    UA_UInt32 recipe_id_in_process = *(UA_UInt32*)recipe_id_in_process_var.data;
    if (recipe_id_in_process == 0)
        cook_next_order();
}

void
robot::cook_next_order() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (order_queue_.empty())
        return;
    order next_order = order_queue_.front();
    order_queue_.pop();
    // Update recipe id in process
    recipe_id_t recipe_id_in_process = next_order.get_recipe_id();
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, RECIPE_ID, &recipe_id_in_process, UA_TYPES_UINT32);
    processed_steps_of_recipe_id_in_process_ = next_order.get_processed_steps();
    // Update dish name
    recipe current_recipe = recipe_parser_.get_recipe(recipe_id_in_process);
    UA_String dish_in_process = UA_STRING(const_cast<char*>(current_recipe.get_dish_name().c_str()));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, DISH_NAME, &dish_in_process, UA_TYPES_STRING);
    action_queue_in_process_ = next_order.get_action_queue();
    determine_next_action();
}

void
robot::compute_overall_time_and_determine_last_tool(std::queue<robot_action> _action_queue) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Get overall time */
    UA_Variant overall_time_var;
    robot_type_inserter_.get_attribute(INSTANCE_NAME, OVERALL_TIME, overall_time_var);
    UA_UInt32 overall_time = *(UA_UInt32*) overall_time_var.data;
    /* Get last equipped tool */
    UA_Variant last_equipped_tool_var;
    robot_type_inserter_.get_attribute(INSTANCE_NAME, LAST_EQUIPPED_TOOL, last_equipped_tool_var);
    robot_tool last_equipped_tool = *(robot_tool*) last_equipped_tool_var.data;
    while (!_action_queue.empty() && capability_parser_.is_capable_to(_action_queue.front().get_name())) {
        overall_time += last_equipped_tool != _action_queue.front().get_required_tool() ? RETOOLING_TIME : 0;
        overall_time += _action_queue.front().get_action_duration();
        last_equipped_tool = _action_queue.front().get_required_tool();
        _action_queue.pop();
    }
    /* Update overall time */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, OVERALL_TIME, &overall_time, UA_TYPES_UINT32);
    /* Update last equipped tool time */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, LAST_EQUIPPED_TOOL, &last_equipped_tool, UA_TYPES_UINT32);
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
    /* Get recipe id in process */
    UA_Variant recipe_id_in_process_var;
    robot_type_inserter_.get_attribute(INSTANCE_NAME, RECIPE_ID, recipe_id_in_process_var);
    UA_UInt32 recipe_id_in_process = *(UA_UInt32*)recipe_id_in_process_var.data;
    /* Set output values */
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &port_, &UA_TYPES[UA_TYPES_UINT16]);
    status |= UA_Variant_setScalarCopy(&_output[1], &position_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[2], &recipe_id_in_process, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[3], &processed_steps_of_recipe_id_in_process_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[4], &next_suitable_robot_port_for_recipe_id_in_process_, &UA_TYPES[UA_TYPES_UINT16]);
    status |= UA_Variant_setScalarCopy(&_output[5], &next_suitable_robot_position_for_recipe_id_in_process_, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output parameters", __FUNCTION__);
        running_ = false;
        return;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "HANDOVER: Pass finished recipe_id=%d (port=%d, position=%d)", recipe_id_in_process, port_, position_);
    /* Set recipe id in process*/
    recipe_id_in_process = 0;
    processed_steps_of_recipe_id_in_process_ = 0;
    next_suitable_robot_port_for_recipe_id_in_process_ = 0;
    next_suitable_robot_position_for_recipe_id_in_process_ = 0;
    /* Update recipe id in process */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, RECIPE_ID, &recipe_id_in_process, UA_TYPES_UINT32);
    /* Update dish in process */
    UA_String dish_in_process = UA_STRING(const_cast<char*>("None"));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, DISH_NAME, &dish_in_process, UA_TYPES_STRING);
    cook_next_order();
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
    /* Get recipe id in process */
    UA_Variant recipe_id_in_process_var;
    robot_type_inserter_.get_attribute(INSTANCE_NAME, RECIPE_ID, recipe_id_in_process_var);
    UA_UInt32 recipe_id_in_process = *(UA_UInt32*)recipe_id_in_process_var.data;
    if (action_queue_in_process_.size()) {
        robot_action robot_act = action_queue_in_process_.front();
        if (!capability_parser_.is_capable_to(robot_act.get_name())) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot is not capable to %s", __FUNCTION__, robot_act.get_name().c_str());
            reset_in_process_fields();
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Request next robot for recipe %d with processed steps %d", recipe_id_in_process, processed_steps_of_recipe_id_in_process_);
            method_node_caller choose_next_robot_caller;
            choose_next_robot_caller.add_scalar_input_argument(&port_, UA_TYPES_UINT16);
            choose_next_robot_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
            choose_next_robot_caller.add_scalar_input_argument(&recipe_id_in_process, UA_TYPES_UINT32);
            choose_next_robot_caller.add_scalar_input_argument(&processed_steps_of_recipe_id_in_process_, UA_TYPES_UINT32);
            UA_ClientConfig* controller_config = UA_Client_getConfig(controller_client_);
            std::string controller_endpoint((char*) controller_config->endpointUrl.data, controller_config->endpointUrl.length);
            object_method_info omi = node_browser_helper().get_method_id(controller_endpoint, CONTROLLER_TYPE, CHOOSE_NEXT_ROBOT);
            if (omi == OBJECT_METHOD_INFO_NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, CHOOSE_NEXT_ROBOT);
                running_ = false;
                return;        
            }
            choose_next_robot_caller.call_method_node(controller_client_, omi.object_id_, omi.method_id_, choose_next_robot_called, this);
            return;
        }
        robot_tool required_tool = robot_act.get_required_tool();
        if (required_tool != current_tool_) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETOOL: Retooling current tool %s to %s", robot_tool_to_string(current_tool_), robot_tool_to_string(required_tool));
            callback_scheduler retool_scheduler(server_, retool, this, NULL);
            UA_StatusCode status = retool_scheduler.schedule_from_now_relative(RETOOLING_TIME * TIME_UNIT);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling retooling", __FUNCTION__);
                running_ = false;
            }
        } else {
            /* Update action in process */
            UA_String action_in_process = UA_STRING(const_cast<char*>(robot_act.get_name().c_str()));
            robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, ACTION_NAME, &action_in_process, UA_TYPES_STRING);
            /* Update ingredients in process */
            UA_String ingredients_in_process = UA_STRING(const_cast<char*>(robot_act.get_ingredients().c_str()));
            robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, INGREDIENTS, &ingredients_in_process, UA_TYPES_STRING);
            /* Schedule next action */
            current_action_duration_ = robot_act.get_action_duration();
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "COOK: Performing %s on recipe_id=%d with ingredients=%s for %ld time units", robot_act.get_name().c_str(), recipe_id_in_process, robot_act.get_ingredients().c_str(), current_action_duration_);
            callback_scheduler action_scheduler(server_, pass_time, this, NULL);
            UA_StatusCode status = action_scheduler.schedule_from_now_relative(TIME_UNIT_UPDATE_RATE * TIME_UNIT);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling pass time", __FUNCTION__);
                running_ = false;
            }
        }
    } else {
        reset_in_process_fields();
        // Send finished order notification to conveyor
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "COOK: Recipe_id=%d finished with %d processed steps, send finished order notification", recipe_id_in_process, processed_steps_of_recipe_id_in_process_);
        method_node_caller receive_finished_order_notification_caller;
        receive_finished_order_notification_caller.add_scalar_input_argument(&port_, UA_TYPES_UINT16);
        receive_finished_order_notification_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
        UA_ClientConfig* conveyor_config = UA_Client_getConfig(conveyor_client_);
        std::string conveyor_endpoint((char*) conveyor_config->endpointUrl.data, conveyor_config->endpointUrl.length);
        object_method_info omi = node_browser_helper().get_method_id(conveyor_endpoint, CONVEYOR_TYPE, FINISHED_ORDER_NOTIFICATION);
        if (omi == OBJECT_METHOD_INFO_NULL) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, FINISHED_ORDER_NOTIFICATION);
            running_ = false;
            return;        
        }
        UA_StatusCode status = receive_finished_order_notification_caller.call_method_node(conveyor_client_, omi.object_id_, omi.method_id_, receive_finished_order_notification_called, this);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to send finished order notification", __FUNCTION__);
            running_ = false;
        }
    }
}

void
robot::reset_in_process_fields() {
    /* Update action in process */
    UA_String action_in_process = UA_STRING(const_cast<char*>("None"));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, ACTION_NAME, &action_in_process, UA_TYPES_STRING);
    /* Update ingredients in process */
    UA_String ingredients_in_process = UA_STRING(const_cast<char*>("None"));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, INGREDIENTS, &ingredients_in_process, UA_TYPES_STRING);
}

void
robot::receive_finished_order_notification_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Userdata is NULL", __FUNCTION__);
        return;
    }
    response_checker response(_response);
    UA_StatusCode status_code = response.get_service_result();
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
    if (!_finished_order_notification_received) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Conveyor returned false", __FUNCTION__);
    }
}

void
robot::pass_time(UA_Server* _server, void* _data) {
    if (_data == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Data is NULL", __FUNCTION__);
        return;
    }
    robot* self = static_cast<robot*>(_data);
    /* Get overall time */
    UA_Variant overall_time_var;
    self->robot_type_inserter_.get_attribute(INSTANCE_NAME, OVERALL_TIME, overall_time_var);
    UA_UInt32 overall_time = *(UA_UInt32*) overall_time_var.data;
    overall_time -= TIME_UNIT_UPDATE_RATE;
    /* Update overall time */
    self->robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, OVERALL_TIME, &overall_time, UA_TYPES_UINT32);
    self->current_action_duration_ -= TIME_UNIT_UPDATE_RATE;
    if (self->current_action_duration_ != 0) {
        callback_scheduler action_scheduler(_server, pass_time, _data, NULL);
        UA_StatusCode status = action_scheduler.schedule_from_now_relative(TIME_UNIT_UPDATE_RATE * TIME_UNIT);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling pass time", __FUNCTION__);
            self->running_ = false;
        }
    } else {
        self->action_performed();
    }
}

void
robot::action_performed() {
    robot_action robot_act = action_queue_in_process_.front();
    duration_t action_duration = robot_act.get_action_duration();
    processed_steps_of_recipe_id_in_process_++;
    /* Get recipe id in process */
    UA_Variant recipe_id_in_process_var;
    robot_type_inserter_.get_attribute(INSTANCE_NAME, RECIPE_ID, recipe_id_in_process_var);
    UA_UInt32 recipe_id_in_process = *(UA_UInt32*)recipe_id_in_process_var.data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "COOK: Performed %s on recipe_id=%d with ingredients=%s for %ld time units", robot_act.get_name().c_str(), recipe_id_in_process, robot_act.get_ingredients().c_str(), action_duration);
    action_queue_in_process_.pop();
    determine_next_action();
}

void
robot::retool(UA_Server* _server, void* _data) {
    if (_data == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Data is NULL", __FUNCTION__);
        return;
    }
    robot* self = static_cast<robot*>(_data);
    self->current_tool_ = self->action_queue_in_process_.front().get_required_tool();
    UA_String current_tool = UA_STRING(const_cast<char*>(robot_tool_to_string(self->current_tool_)));
    /* Update current tool */
    self->robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, CURRENT_TOOL, &current_tool, UA_TYPES_STRING);
    /* Get overall time */
    UA_Variant overall_time_var;
    self->robot_type_inserter_.get_attribute(INSTANCE_NAME, OVERALL_TIME, overall_time_var);
    UA_UInt32 overall_time = *(UA_UInt32*) overall_time_var.data;
    overall_time -= RETOOLING_TIME;
    /* Update overall time */
    self->robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, OVERALL_TIME, &overall_time, UA_TYPES_UINT32);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETOOL: Current tool now is %s", robot_tool_to_string(self->current_tool_));
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

void
robot::start() {
    if (!running_)
        return;
    method_node_caller register_robot_caller;
    register_robot_caller.add_scalar_input_argument(&port_, UA_TYPES_UINT16);
    register_robot_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
    UA_Variant capabilities;
    robot_type_inserter_.get_attribute(INSTANCE_NAME, CAPABILITIES, capabilities);
    register_robot_caller.add_array_input_argument(capabilities.data, capabilities.arrayLength, UA_TYPES_STRING);
    UA_ClientConfig* controller_config = UA_Client_getConfig(controller_client_);
    std::string controller_endpoint((char*) controller_config->endpointUrl.data, controller_config->endpointUrl.length);
    object_method_info omi = node_browser_helper().get_method_id(controller_endpoint, CONTROLLER_TYPE, REGISTER_ROBOT);
    if (omi == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, REGISTER_ROBOT);
        running_ = false;
        join_threads();
        return;        
    }
    UA_StatusCode status = register_robot_caller.call_method_node(controller_client_, omi.object_id_, omi.method_id_, register_robot_called, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling the register robot method node", __FUNCTION__);
        running_ = false;
    }
    join_threads();
}

void
robot::stop() {
    running_ = false;
}