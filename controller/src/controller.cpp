#include "../include/controller.hpp"
#include <open62541/server_config_default.h>
#include <string>
#include <chrono>
#include "filtered_logger.hpp"

#define INSTANCE_NAME "KitchenController"
#define RECIPE_PATH "recipes.json"

controller::controller() : server_(UA_Server_new()), controller_type_inserter_(server_, CONTROLLER_TYPE), running_(true), recipe_parser_(RECIPE_PATH), mersenne_twister_(random_device_()), uniform_int_distribution_(1,3) {
    /* Setup controller */
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the controller server");
        running_ = false;
        return;
    }
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC("urn:kitchen:controller");
    // *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add choose next robot method node */
    method_arguments choose_next_robot_arguments;
    choose_next_robot_arguments.add_input_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_input_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_input_argument("the processed steps", "processed_steps", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_output_argument("the next suitable robot endpoint", "next_suitable_robot_endpoint", UA_TYPES_STRING);
    choose_next_robot_arguments.add_output_argument("the next suitable robot position", "next_suitable_robot_position", UA_TYPES_UINT32);
    status = controller_type_inserter_.add_method(CONTROLLER_TYPE, CHOOSE_NEXT_ROBOT, choose_next_robot, choose_next_robot_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, CHOOSE_NEXT_ROBOT);
        running_ = false;
        return;
    }
    /* Add register robot method node */
    method_arguments register_robot_arguments;
    register_robot_arguments.add_input_argument("the robot endpoint", "robot_endpoint", UA_TYPES_STRING);
    register_robot_arguments.add_input_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    register_robot_arguments.add_input_argument("the robot capabilities", "robot_capabilities", UA_TYPES_STRING);
    register_robot_arguments.add_output_argument("indicates whether the capabilities are received", "capabilities_received", UA_TYPES_BOOLEAN);
    status = controller_type_inserter_.add_method(CONTROLLER_TYPE, REGISTER_ROBOT, register_robot, register_robot_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, REGISTER_ROBOT);
        running_ = false;
        return;
    }
    /* Add place random order method node */
    method_arguments place_random_order_arguments;
    place_random_order_arguments.add_output_argument("indicates whether the robot is instructed", "robot_instructed", UA_TYPES_BOOLEAN);
    status = controller_type_inserter_.add_method(CONTROLLER_TYPE, PLACE_RANDOM_ORDER, place_random_order, place_random_order_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, PLACE_RANDOM_ORDER);
        running_ = false;
        return;
    }
    /* Add controller type constructor */
    controller_type_inserter_.add_object_type_constructor(server_, controller_type_inserter_.get_object_type_id(CONTROLLER_TYPE));
    /* Instantiate controller type */
    controller_type_inserter_.add_object_instance(INSTANCE_NAME, CONTROLLER_TYPE);
    /* Run the controller server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error at controller startup");
        running_ = false;
        return;
    }
    /* Register at discovery server repeatedly */
    discovery_util_.register_server_repeatedly(server_);
    /* Start the controller event loop */
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running controller");
        running_ = false;
        return;
    }
}

controller::~controller() {
    stop();
    join_threads();
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
}

UA_StatusCode
controller::register_robot(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 3) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    
    UA_StatusCode status = !UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_STRING]);
    status |= !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasArrayType(&_input[2], &UA_TYPES[UA_TYPES_STRING]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract input arguments */
    UA_String endpoint_tmp = *(UA_String*)_input[0].data;
    std::string endpoint((char*) endpoint_tmp.data, endpoint_tmp.length);
    position_t position = *(position_t*)_input[1].data;
    std::unordered_set<std::string> remote_robot_capabilities;
    for (size_t i = 0; i < _input[2].arrayLength; i++) {
        UA_String capability = ((UA_String*)_input[2].data)[i];
        remote_robot_capabilities.insert(std::string((char*) capability.data, capability.length));
    }
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(_method_context);
    self->handle_robot_registration(endpoint, position, remote_robot_capabilities, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_robot_registration(std::string _endpoint, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::string capabilites_str = "REGISTRATION: Capabilities of robot at position " + std::to_string(_position) + " [";
    for (std::string capability : _remote_robot_capabilities) {
        capabilites_str += capability + ", ";
    }
    capabilites_str.erase(capabilites_str.end()-2, capabilites_str.end());
    capabilites_str += "]";
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: %s", __FUNCTION__, capabilites_str.c_str());
    remove_marked_robots();
    if (position_remote_robot_map_.find(_position) == position_remote_robot_map_.end()) {
        position_remote_robot_map_[_position] = std::make_unique<remote_robot>(_endpoint, _position, _remote_robot_capabilities, std::bind(&controller::mark_robot_for_removal, this, std::placeholders::_1));
    }
    bool robot_registration_success = true;
    if (robots_to_be_removed_.find(_position) != robots_to_be_removed_.end()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot registration at position %d failed", __FUNCTION__, _position);
        remove_marked_robots();
        robot_registration_success = false;
    }
    UA_Variant_setScalarCopy(_output, &robot_registration_success, &UA_TYPES[UA_TYPES_BOOLEAN]);
}

UA_StatusCode
controller::choose_next_robot(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 3) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    
    UA_StatusCode status = !UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[2], &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract input arguments */
    position_t position = *(position_t*)_input[0].data;
    recipe_id_t recipe_id = *(recipe_id_t*)_input[1].data;
    UA_UInt32 processed_steps = *(UA_UInt32*)_input[2].data;
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(_method_context);
    self->handle_next_robot_request(position, recipe_id, processed_steps, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_next_robot_request(position_t _position, recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (position_remote_robot_map_.find(_position) == position_remote_robot_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d must register first", __FUNCTION__, _position);
        return;
    }

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Robot at position %d requests next robot for recipe id %d processed with %d steps already", _position, _recipe_id, _processed_steps);
    remote_robot* next_suitable_robot = find_suitable_robot(_recipe_id, _processed_steps);
    UA_String next_suitable_robot_endpoint = UA_STRING_ALLOC("");
    position_t next_suitable_robot_position = 0;
    if (next_suitable_robot != NULL) {
        UA_String_clear(&next_suitable_robot_endpoint);
        next_suitable_robot_endpoint = UA_STRING_ALLOC(next_suitable_robot->get_endpoint().c_str());
        next_suitable_robot_position = next_suitable_robot->get_position();
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Next robot is at position %d (%s)", next_suitable_robot_position, next_suitable_robot->get_endpoint().c_str());
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &next_suitable_robot_endpoint, &UA_TYPES[UA_TYPES_STRING]);
    status |= UA_Variant_setScalarCopy(&_output[1], &next_suitable_robot_position, &UA_TYPES[UA_TYPES_UINT32]);
    UA_String_clear(&next_suitable_robot_endpoint);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output parameters", __FUNCTION__);
        stop();
        return;
    }
}

remote_robot*
controller::find_suitable_robot(recipe_id_t _recipe_id, UA_UInt32 _processed_steps) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    remove_marked_robots();
    std::queue<robot_action> recipe_action_queue = recipe_parser_.get_recipe(_recipe_id).get_action_queue();
    for (size_t i = 0; i < _processed_steps; i++) {
        recipe_action_queue.pop();
    }
    remote_robot* suitable_robot = NULL;
    std::string next_action = recipe_action_queue.front().get_name();
    for (auto position_remote_robot = position_remote_robot_map_.begin(); position_remote_robot != position_remote_robot_map_.end(); position_remote_robot++) {
        remote_robot* robot = position_remote_robot->second.get();
        if (robot->is_capable_to(next_action)) {
            suitable_robot = robot;
            break;
        }
    }
    return suitable_robot;
}

UA_StatusCode
controller::place_random_order(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    if(_input_size != 0) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(_method_context);
    self->handle_random_order_request(_output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_random_order_request(UA_Variant* _output) {
    bool instructed = false;
    recipe_id_t recipe_id = uniform_int_distribution_(mersenne_twister_);
    remote_robot* next_suitable_robot = find_suitable_robot(recipe_id, 0);
    if (next_suitable_robot != NULL) {
        UA_Variant* output;
        size_t output_size;
        UA_StatusCode status = next_suitable_robot->instruct(recipe_id, 0, &output_size, &output);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Calling instruct on remote robot failed", __FUNCTION__);
            instructed = false;
            UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);
            return;
        }
        receive_robot_task_called(output_size, output);
        instructed = true;
    }
    UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);
}

void
controller::receive_robot_task_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_UINT32])
       || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }


    position_t remote_robot_position = *(position_t*) _output[0].data;
    UA_Boolean result = *(UA_Boolean*) _output[1].data;

    remote_robot* robot = position_remote_robot_map_[remote_robot_position].get();
    // Sanity check
    if(robot->get_position() != remote_robot_position) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Mismatch on position. Received position %d, actually %d", __FUNCTION__, remote_robot_position, robot->get_position());
    }
    if (!result)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d returned false", __FUNCTION__, robot->get_position());
}

void
controller::mark_robot_for_removal(position_t _position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    robots_to_be_removed_.insert(_position);
}

void
controller::remove_marked_robots() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (position_t position : robots_to_be_removed_) {
        if (position_remote_robot_map_.find(position) != position_remote_robot_map_.end()) {
            position_remote_robot_map_.erase(position);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Removed remote robot at position %d", position);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "No remote robot found at position %d", position);
        }
    }
    robots_to_be_removed_.clear();
}

void
controller::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
}

void
controller::start() {
    if (!running_)
        stop();
    join_threads();
}

void
controller::stop() {
    running_ = false;
    discovery_util_.stop();
    discovery_util_.deregister_server(server_);
}