#include "../include/robot.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <string>
#include <chrono>
#include "client_connection_establisher.hpp"
#include "time_unit.hpp"
#include "filtered_logger.hpp"
#include "browsenames.h"
#include "discovery_and_connection.hpp"

#define INSTANCE_NAME "KitchenRobot"
#define RECIPE_PATH "recipes.json"
#define CAPABILITIES_PATH "./capabilities/"

robot::robot(position_t _position) :
        server_(UA_Server_new()), position_(_position), robot_uri_("urn:kitchen:robot:" + std::to_string(position_)), robot_type_inserter_(server_, ROBOT_TYPE), preparing_dish_(false), is_dish_finished_(false), running_(true), current_tool_(robot_tool::ROBOT_TOOLS_COUNT),
        processed_steps_of_recipe_id_in_process_(0), current_action_duration_(0),
        recipe_parser_(RECIPE_PATH), capability_parser_(CAPABILITIES_PATH, _position), work_guard_(boost::asio::make_work_guard(io_context_)), steady_timer_(io_context_), controller_client_(nullptr), conveyor_client_(nullptr) {
    /* Setup robot */
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error with setting up the server", __FUNCTION__);
        running_ = false;
        return;
    }
    // Set a unique application URI for the robot
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC(robot_uri_.c_str());
    // *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add attributes */
    robot_type_inserter_.add_attribute(ROBOT_TYPE, POSITION);
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
    handover_finished_order_method_arguments.add_output_argument("the robot endpoint", "robot_endpoint", UA_TYPES_STRING);
    handover_finished_order_method_arguments.add_output_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    handover_finished_order_method_arguments.add_output_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    handover_finished_order_method_arguments.add_output_argument("the processed steps", "processed_steps", UA_TYPES_UINT32);
    handover_finished_order_method_arguments.add_output_argument("is dish finished", "is_dish_finished", UA_TYPES_BOOLEAN);
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
    /* Set position at conveyor */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, POSITION, &position_, UA_TYPES_UINT32);
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
        UA_String_init(&(ua_capabilities[i]));
        UA_String tmp = UA_STRING(const_cast<char*>(capability.c_str()));
        UA_String_copy(&tmp, &(ua_capabilities[i]));
        i++;
    }
    robot_type_inserter_.set_array_attribute(INSTANCE_NAME, CAPABILITIES, ua_capabilities, capabilities.size(), UA_TYPES_STRING);
    for (size_t i = 0; i < capabilities.size(); i++) {
        UA_String_clear(&(ua_capabilities[i]));
    }
    
    /* Run the robot server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at robot startup", __FUNCTION__);
        running_ = false;
        return;
    }
    /* Register at discovery server repeatedly */
    if (discovery_util_.register_server_repeatedly(server_) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to start discovery register", __FUNCTION__);
        stop();
        return;
    }
    /* Start the robot eventloop */
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot", __FUNCTION__);
        stop();
        return;
    }
    /* Setup controller client */
    std::string controller_endpoint;
    while((status = discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE)) != UA_STATUSCODE_GOOD) {
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        if (!running_) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to controller", __FUNCTION__);
            stop();
            return;
        }
    }
    /* Gather method ids */
    if ((method_id_map_[REGISTER_ROBOT] = node_browser_helper().get_method_id(controller_endpoint, CONTROLLER_TYPE, REGISTER_ROBOT)) == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, REGISTER_ROBOT);
        stop();
        return;        
    }
    /* Setup conveyor client */
    std::string conveyor_endpoint;
    while((status = discover_and_connect(conveyor_client_, discovery_util_, conveyor_endpoint, CONVEYOR_TYPE)) != UA_STATUSCODE_GOOD) {
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        if (!running_) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to conveyor", __FUNCTION__);
            stop();
            return;
        }
    }
    /* Gather method ids */
    if ((method_id_map_[FINISHED_ORDER_NOTIFICATION] = node_browser_helper().get_method_id(conveyor_endpoint, CONVEYOR_TYPE, FINISHED_ORDER_NOTIFICATION)) == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, FINISHED_ORDER_NOTIFICATION);
        stop();
        return;        
    }
}

void
robot::register_robot_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return;
    }
    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return;
    }
    UA_Boolean register_robot_received = *(UA_Boolean*) _output[0].data;
    if (!register_robot_received) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Controller returned false", __FUNCTION__);
    }
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
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

    if (!UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT32])
      ||!UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32])) {
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
    // Set output parameters
    UA_Boolean task_received = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &self->position_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[1], &task_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error returning states", __FUNCTION__);
        self->stop();
        return UA_STATUSCODE_BAD;
    }
    self->io_context_.post([self, recipe_id, processed_steps] {
        self->handle_receive_task(recipe_id, processed_steps);
    });
    return UA_STATUSCODE_GOOD;
}

void
robot::handle_receive_task(recipe_id_t _recipe_id, UA_UInt32 _processed_steps) {
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
    if (!preparing_dish_) {
        cook_next_order();
    }
}

void
robot::cook_next_order() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (order_queue_.empty()) {
        preparing_dish_ = false;
        return;
    }
    preparing_dish_ = true;
    order next_order = order_queue_.front();
    order_queue_.pop();
    // Update recipe id in process
    recipe_id_t recipe_id_in_process = next_order.get_recipe_id();
    UA_StatusCode status = robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, RECIPE_ID, &recipe_id_in_process, UA_TYPES_UINT32);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Setting %s failed", __FUNCTION__, RECIPE_ID);
    }
    processed_steps_of_recipe_id_in_process_ = next_order.get_processed_steps();
    // Update dish name
    recipe current_recipe = recipe_parser_.get_recipe(recipe_id_in_process);
    UA_String dish_in_process = UA_STRING_ALLOC(current_recipe.get_dish_name().c_str());
    status = robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, DISH_NAME, &dish_in_process, UA_TYPES_STRING);
    UA_String_clear(&dish_in_process);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Setting %s failed", __FUNCTION__, DISH_NAME);
    }
    action_queue_in_process_ = next_order.get_action_queue();
    determine_next_action();
}

void
robot::compute_overall_time_and_determine_last_tool(std::queue<robot_action> _action_queue) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Get overall time */
    UA_Variant overall_time_var;
    UA_Variant_init(&overall_time_var);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, OVERALL_TIME, overall_time_var);
    UA_UInt32 overall_time = *(UA_UInt32*) overall_time_var.data;
    UA_Variant_clear(&overall_time_var);
    /* Get last equipped tool */
    UA_Variant last_equipped_tool_var;
    UA_Variant_init(&last_equipped_tool_var);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, LAST_EQUIPPED_TOOL, last_equipped_tool_var);
    robot_tool last_equipped_tool = *(robot_tool*) last_equipped_tool_var.data;
    UA_Variant_clear(&last_equipped_tool_var);
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Get recipe id in process */
    UA_Variant recipe_id_in_process_var;
    UA_Variant_init(&recipe_id_in_process_var);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, RECIPE_ID, recipe_id_in_process_var);
    UA_UInt32 recipe_id_in_process = *(UA_UInt32*)recipe_id_in_process_var.data;
    UA_Variant_clear(&recipe_id_in_process_var);
    /* Set output values */
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &server_endpoint_, &UA_TYPES[UA_TYPES_STRING]);
    status |= UA_Variant_setScalarCopy(&_output[1], &position_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[2], &recipe_id_in_process, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[3], &processed_steps_of_recipe_id_in_process_, &UA_TYPES[UA_TYPES_UINT32]);
    status |= UA_Variant_setScalarCopy(&_output[4], &is_dish_finished_, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output parameters", __FUNCTION__);
        stop();
        return;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "HANDOVER: Pass finished recipe_id=%d from position %d", recipe_id_in_process, position_);
    /* Set recipe id in process*/
    recipe_id_in_process = 0;
    processed_steps_of_recipe_id_in_process_ = 0;
    is_dish_finished_ = false;
    /* Update recipe id in process */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, RECIPE_ID, &recipe_id_in_process, UA_TYPES_UINT32);
    /* Update dish in process */
    UA_String dish_in_process = UA_STRING(const_cast<char*>("None"));
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, DISH_NAME, &dish_in_process, UA_TYPES_STRING);
    io_context_.post([this] {
        cook_next_order();
    });
}

robot::~robot() {
    running_ = false;
    join_threads();
    UA_String_clear(&server_endpoint_);
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    if (controller_client_ != nullptr)
        UA_Client_delete(controller_client_);
    if (conveyor_client_ != nullptr)
        UA_Client_delete(conveyor_client_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Destructor finished successfully", __FUNCTION__);
}

void
robot::determine_next_action() {
    /* Get recipe id in process */
    UA_Variant recipe_id_in_process_var;
    UA_Variant_init(&recipe_id_in_process_var);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, RECIPE_ID, recipe_id_in_process_var);
    UA_UInt32 recipe_id_in_process = *(UA_UInt32*)recipe_id_in_process_var.data;
    UA_Variant_clear(&recipe_id_in_process_var);
    /* Process remaining actions */
    if (action_queue_in_process_.size()) {
        robot_action robot_act = action_queue_in_process_.front();
        /* Request next robot if not capable to process the action */
        if (!capability_parser_.is_capable_to(robot_act.get_name())) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot is not capable to %s", __FUNCTION__, robot_act.get_name().c_str());
            reset_in_process_fields();
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "COOK: Recipe_id=%d finished with %d processed steps, send partially finished order notification", recipe_id_in_process, processed_steps_of_recipe_id_in_process_);
            is_dish_finished_ = false;
            /* Notify conveyor about finished order */
            method_node_caller receive_finished_order_notification_caller;
            receive_finished_order_notification_caller.add_scalar_input_argument(&server_endpoint_, UA_TYPES_STRING);
            receive_finished_order_notification_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
            object_method_info omi = method_id_map_[FINISHED_ORDER_NOTIFICATION];
            size_t output_size = 0;
            UA_Variant* output = nullptr;
            UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
            while (status != UA_STATUSCODE_GOOD) {
                {
                    std::unique_lock<std::mutex> lock(client_mutex_);
                    if (conveyor_client_ != nullptr)
                        status = receive_finished_order_notification_caller.call_method_node(conveyor_client_, omi.object_id_, omi.method_id_, &output_size, &output);
                    if (running_ && status != UA_STATUSCODE_GOOD) {
                        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error sending finished order notification (%s)", __FUNCTION__, UA_StatusCode_name(status));
                        if (output != nullptr) {
                            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                            output_size = 0;
                            output = nullptr;
                        }
                        conveyor_connected_condition_.wait(lock);
                    }
                    if(!running_) {
                        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to send finished order notification (%s)", __FUNCTION__, UA_StatusCode_name(status));
                        if (output != nullptr)
                            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                        return;
                    }
                }
            }
            receive_finished_order_notification_called(output_size, output);
            return;
        }
        /* Retool if necessary */
        robot_tool required_tool = robot_act.get_required_tool();
        if (required_tool != current_tool_) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETOOL: Retooling current tool %s to %s", robot_tool_to_string(current_tool_), robot_tool_to_string(required_tool));
            steady_timer_.expires_from_now(std::chrono::milliseconds(RETOOLING_TIME * TIME_UNIT));
            steady_timer_.async_wait([this](const boost::system::error_code& _error) {
                if (_error) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling retooling", __FUNCTION__);
                    stop();
                    return;
                }
                retool();
            });
        /* Process the next action */
        } else {
            /* Update action in process */
            UA_String action_in_process = UA_STRING(const_cast<char*>(robot_act.get_name().c_str()));
            robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, ACTION_NAME, &action_in_process, UA_TYPES_STRING);
            /* Update ingredients in process */
            UA_String ingredients_in_process = UA_STRING_ALLOC(robot_act.get_ingredients().c_str());
            robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, INGREDIENTS, &ingredients_in_process, UA_TYPES_STRING);
            UA_String_clear(&ingredients_in_process);
            /* Schedule next action */
            current_action_duration_ = robot_act.get_action_duration();
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "COOK: Performing %s on recipe_id=%d with ingredients=%s for %ld time units", robot_act.get_name().c_str(), recipe_id_in_process, robot_act.get_ingredients().c_str(), current_action_duration_);
            steady_timer_.expires_from_now(std::chrono::milliseconds(TIME_UNIT_UPDATE_RATE * TIME_UNIT));
            steady_timer_.async_wait([this](const boost::system::error_code& _error) {
                if (_error) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling pass time (%s)", __FUNCTION__, _error.what().c_str());
                    stop();
                    return;
                }
                pass_time();
            });
        }
    } else {
        reset_in_process_fields();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "COOK: Recipe_id=%d finished with %d processed steps, send finished order notification", recipe_id_in_process, processed_steps_of_recipe_id_in_process_);
        is_dish_finished_ = true;
        /* Notify conveyor about finished order */
        method_node_caller receive_finished_order_notification_caller;
        receive_finished_order_notification_caller.add_scalar_input_argument(&server_endpoint_, UA_TYPES_STRING);
        receive_finished_order_notification_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
        object_method_info omi = method_id_map_[FINISHED_ORDER_NOTIFICATION];
        size_t output_size = 0;
        UA_Variant* output = nullptr;
        UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
        while (status != UA_STATUSCODE_GOOD) {
            {
                std::unique_lock<std::mutex> lock(client_mutex_);
                if (conveyor_client_ != nullptr)
                    status = receive_finished_order_notification_caller.call_method_node(conveyor_client_, omi.object_id_, omi.method_id_, &output_size, &output);
                if (running_ && status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error sending finished order notification (%s)", __FUNCTION__, UA_StatusCode_name(status));
                    if (output != nullptr) {
                        UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                        output_size = 0;
                        output = nullptr;
                    }
                    conveyor_connected_condition_.wait(lock);
                }
                if(!running_) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to send finished order notification (%s)", __FUNCTION__, UA_StatusCode_name(status));
                    if (output != nullptr)
                        UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                    return;
                }
            }
        }
        receive_finished_order_notification_called(output_size, output);
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
robot::receive_finished_order_notification_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return;
    }
    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return;
    }
    UA_Boolean finished_order_notification_received = *(UA_Boolean*) _output[0].data;
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, finished_order_notification_received);
    if (!finished_order_notification_received) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Conveyor returned false", __FUNCTION__);
    }
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
}

void
robot::pass_time() {
    /* Get overall time */
    UA_Variant overall_time_var;
    UA_Variant_init(&overall_time_var);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, OVERALL_TIME, overall_time_var);
    UA_UInt32 overall_time = *(UA_UInt32*) overall_time_var.data;
    UA_Variant_clear(&overall_time_var);
    overall_time -= TIME_UNIT_UPDATE_RATE;
    /* Update overall time */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, OVERALL_TIME, &overall_time, UA_TYPES_UINT32);
    current_action_duration_ -= TIME_UNIT_UPDATE_RATE;
    if (current_action_duration_ != 0) {
        steady_timer_.expires_from_now(std::chrono::milliseconds(TIME_UNIT_UPDATE_RATE * TIME_UNIT));
        steady_timer_.async_wait([this](const boost::system::error_code& _error) {
            if (_error) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling pass time (%s)", __FUNCTION__, _error.what().c_str());
                stop();
                return;
            }
            pass_time();
        });
    } else {
        action_performed();
    }
}

void
robot::action_performed() {
    robot_action robot_act = action_queue_in_process_.front();
    duration_t action_duration = robot_act.get_action_duration();
    processed_steps_of_recipe_id_in_process_++;
    /* Get recipe id in process */
    UA_Variant recipe_id_in_process_var;
    UA_Variant_init(&recipe_id_in_process_var);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, RECIPE_ID, recipe_id_in_process_var);
    UA_UInt32 recipe_id_in_process = *(UA_UInt32*)recipe_id_in_process_var.data;
    UA_Variant_clear(&recipe_id_in_process_var);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "COOK: Performed %s on recipe_id=%d with ingredients=%s for %ld time units", robot_act.get_name().c_str(), recipe_id_in_process, robot_act.get_ingredients().c_str(), action_duration);
    action_queue_in_process_.pop();
    determine_next_action();
}

void
robot::retool() {
    current_tool_ = action_queue_in_process_.front().get_required_tool();
    UA_String current_tool = UA_STRING(const_cast<char*>(robot_tool_to_string(current_tool_)));
    /* Update current tool */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, CURRENT_TOOL, &current_tool, UA_TYPES_STRING);
    /* Get overall time */
    UA_Variant overall_time_var;
    UA_Variant_init(&overall_time_var);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, OVERALL_TIME, overall_time_var);
    UA_UInt32 overall_time = *(UA_UInt32*) overall_time_var.data;
    UA_Variant_clear(&overall_time_var);
    overall_time -= RETOOLING_TIME;
    /* Update overall time */
    robot_type_inserter_.set_scalar_attribute(INSTANCE_NAME, OVERALL_TIME, &overall_time, UA_TYPES_UINT32);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETOOL: Current tool now is %s", robot_tool_to_string(current_tool_));
    determine_next_action();
}

void
robot::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if(worker_thread_.joinable())
        worker_thread_.join();
    if (client_iterate_thread_.joinable())
        client_iterate_thread_.join();
}

void
robot::start() {
    if (!running_) {
        stop();
        return;
    }
    std::vector<std::string> endpoints;
    while (endpoints.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Looking up own endpoint", __FUNCTION__);
        if (discovery_util_.lookup_endpoints(endpoints, robot_uri_) != UA_STATUSCODE_GOOD || endpoints.empty()) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Couldn't look up own endpoint. Trying again in %d seconds", __FUNCTION__, LOOKUP_INTERVAL);
            std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        }
        if (!running_) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error looking up own endpoint url", __FUNCTION__);
            stop();
            return;
        }
    }
    UA_String_init(&server_endpoint_);
    server_endpoint_ = UA_STRING_ALLOC(const_cast<char*>(endpoints[0].c_str()));
    method_node_caller register_robot_caller;
    register_robot_caller.add_scalar_input_argument(&server_endpoint_, UA_TYPES_STRING);
    register_robot_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
    UA_Variant capabilities;
    UA_Variant_init(&capabilities);
    robot_type_inserter_.get_attribute(INSTANCE_NAME, CAPABILITIES, capabilities);
    register_robot_caller.add_array_input_argument(capabilities.data, capabilities.arrayLength, UA_TYPES_STRING);
    UA_Variant_clear(&capabilities);
    object_method_info omi = method_id_map_[REGISTER_ROBOT];

    worker_thread_ = std::thread([this]() {
        io_context_.run();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited io_context", __FUNCTION__);
    });

    size_t output_size = 0;
    UA_Variant* output = nullptr;
    UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
    while (status != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Registering at the controller", __FUNCTION__);
        if ((controller_client_ != nullptr) && (status = register_robot_caller.call_method_node(controller_client_, omi.object_id_, omi.method_id_, &output_size, &output)) != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling the register robot method node", __FUNCTION__);
            if (output != nullptr) {
                UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                output_size = 0;
                output = nullptr;
            }
            std::string controller_endpoint;
            UA_Client_delete(controller_client_);
            controller_client_ = nullptr;
            discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE);
            std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));

        }
        if (!running_) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error registering at the controller", __FUNCTION__);
            if (output != nullptr)
                UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
            stop();
            return;
        }
    }
    register_robot_called(output_size, output);
    /* Run the client iterate thread */
    try {
        client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    if (controller_client_ != nullptr) {
                        UA_StatusCode status = UA_Client_run_iterate(controller_client_, 1);
                        if (status != UA_STATUSCODE_GOOD) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running controller client iterate", __FUNCTION__);
                            UA_Client_delete(controller_client_);
                            controller_client_ = nullptr;
                        }
                    } else {
                        std::string controller_endpoint;
                        if (discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE) == UA_STATUSCODE_GOOD) {
                            method_node_caller register_robot_caller;
                            register_robot_caller.add_scalar_input_argument(&server_endpoint_, UA_TYPES_STRING);
                            register_robot_caller.add_scalar_input_argument(&position_, UA_TYPES_UINT32);
                            UA_Variant capabilities;
                            UA_Variant_init(&capabilities);
                            robot_type_inserter_.get_attribute(INSTANCE_NAME, CAPABILITIES, capabilities);
                            register_robot_caller.add_array_input_argument(capabilities.data, capabilities.arrayLength, UA_TYPES_STRING);
                            UA_Variant_clear(&capabilities);
                            object_method_info omi = method_id_map_[REGISTER_ROBOT];
                            size_t output_size = 0;
                            UA_Variant* output = nullptr;
                            UA_StatusCode status = register_robot_caller.call_method_node(controller_client_, omi.object_id_, omi.method_id_, &output_size, &output);
                            if (status != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed calling robot register during client iterate", __FUNCTION__);
                                if (output != nullptr)
                                    UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                            } else {
                                register_robot_called(output_size, output);
                            }
                        }
                    }

                    if (conveyor_client_ != nullptr) {
                        UA_StatusCode status = UA_Client_run_iterate(conveyor_client_, 1);
                        if (status != UA_STATUSCODE_GOOD) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running conveyor client iterate", __FUNCTION__);
                            UA_Client_delete(conveyor_client_);
                            conveyor_client_ = nullptr;
                        }
                    } else {
                        std::string conveyor_endpoint;
                        if (discover_and_connect(conveyor_client_, discovery_util_, conveyor_endpoint, CONVEYOR_TYPE) == UA_STATUSCODE_GOOD)
                            conveyor_connected_condition_.notify_all();
                    }
                }
                if (usleep(1*1000)) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at client iterate sleep", __FUNCTION__);
                    stop();
                    return;
                }
                // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Starting the next client iterate", __FUNCTION__);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the client iterate thread");
        stop();
        return;
    }
    join_threads();
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited start method", __FUNCTION__);
}

void
robot::stop() {
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        running_ = false;
        conveyor_connected_condition_.notify_all();
    }
    work_guard_.reset();
    io_context_.stop();
    discovery_util_.stop();
    discovery_util_.deregister_server(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Stop finished successfully", __FUNCTION__);
}