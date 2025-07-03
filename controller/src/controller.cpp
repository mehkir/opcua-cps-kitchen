#include "../include/controller.hpp"
#include <open62541/server_config_default.h>
#include <string>
#include <response_checker.hpp>

#define INSTANCE_NAME "KitchenController"
#define RECIPE_PATH "recipes.json"

controller::controller(port_t _port) : server_(UA_Server_new()), port_(_port), controller_type_inserter_(server_, CONTROLLER_TYPE), running_(true), recipe_parser_(RECIPE_PATH), mersenne_twister_(random_device_()), uniform_int_distribution_(1,3) {
    /* Setup controller */
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the controller server");
        running_ = false;
        return;
    }
    /* Add choose next robot method node */
    method_arguments choose_next_robot_arguments;
    choose_next_robot_arguments.add_input_argument("the robot port", "robot_port", UA_TYPES_UINT16);
    choose_next_robot_arguments.add_input_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_input_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_input_argument("the processed steps", "processed_steps", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_output_argument("the next suitable robot port", "next_suitable_robot_port", UA_TYPES_UINT16);
    choose_next_robot_arguments.add_output_argument("the next suitable robot position", "next_suitable_robot_position", UA_TYPES_UINT32);
    status = controller_type_inserter_.add_method(CONTROLLER_TYPE, CHOOSE_NEXT_ROBOT, choose_next_robot, choose_next_robot_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, CHOOSE_NEXT_ROBOT);
        running_ = false;
        return;
    }
    /* Add register robot method node */
    method_arguments register_robot_arguments;
    register_robot_arguments.add_input_argument("the robot port", "robot_port", UA_TYPES_UINT16);
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
    running_ = false;
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
    
    UA_StatusCode status = !UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT16]);
    status |= !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasArrayType(&_input[2], &UA_TYPES[UA_TYPES_STRING]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract input arguments */
    port_t port = *(port_t*)_input[0].data;
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
    self->handle_robot_registration(port, position, remote_robot_capabilities, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_robot_registration(port_t _port, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::string capabilites_str = "REGISTRATION: Capabilities of robot at position " + std::to_string(_position) + " [";
    for (std::string capability : _remote_robot_capabilities) {
        capabilites_str += capability + ", ";
    }
    capabilites_str.erase(capabilites_str.end()-2, capabilites_str.end());
    capabilites_str += "]";
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: %s", __FUNCTION__, capabilites_str.c_str());
    if (position_remote_robot_map_.find(_position) == position_remote_robot_map_.end()) {
        position_remote_robot_map_[_position] = std::make_unique<remote_robot>(_port, _position, _remote_robot_capabilities);
        position_remote_robot_map_[_position]->start_thread();
    }
    bool capabilities_received = true;
    UA_Variant_setScalarCopy(_output, &capabilities_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
}

UA_StatusCode
controller::choose_next_robot(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 4) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    UA_StatusCode status = !UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT16]);
    status |= !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[2], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[3], &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract input arguments */
    port_t port = *(port_t*)_input[0].data;
    position_t position = *(position_t*)_input[1].data;
    recipe_id_t recipe_id = *(recipe_id_t*)_input[2].data;
    UA_UInt32 processed_steps = *(UA_UInt32*)_input[3].data;
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(_method_context);
    self->handle_next_robot_request(port, position, recipe_id, processed_steps, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_next_robot_request(port_t _port, position_t _position, recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (position_remote_robot_map_.find(_position) == position_remote_robot_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d must register first", __FUNCTION__, _position);
        return;
    }

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Robot with port %d and position %d requests next robot for recipe id %d processed with %d steps already", _port, _position, _recipe_id, _processed_steps);
    remote_robot* next_suitable_robot = find_suitable_robot(_recipe_id, _processed_steps);
    port_t next_suitable_robot_port = 0;
    position_t next_suitable_robot_position = 0;
    if (next_suitable_robot != NULL) {
        next_suitable_robot_port = next_suitable_robot->get_port();
        next_suitable_robot_position = next_suitable_robot->get_position();
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Next robot is at position %d with port %d", next_suitable_robot_position, next_suitable_robot_port);
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &next_suitable_robot_port, &UA_TYPES[UA_TYPES_UINT16]);
    status |= UA_Variant_setScalarCopy(&_output[1], &next_suitable_robot_position, &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output parameters", __FUNCTION__);
        running_ = false;
        return;
    }
}

remote_robot*
controller::find_suitable_robot(recipe_id_t _recipe_id, UA_UInt32 _processed_steps) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
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
        next_suitable_robot->instruct(recipe_id, 0, receive_robot_task_called);
        instructed = true;
    }
    UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);   
}

void
controller::receive_robot_task_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
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

    if(response.get_output_arguments_size(0) != 3) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return;
    }

    if(!response.has_scalar_type(0, 0, &UA_TYPES[UA_TYPES_UINT16])
      || !response.has_scalar_type(0, 1, &UA_TYPES[UA_TYPES_UINT32])
      || !response.has_scalar_type(0, 2, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }

    port_t remote_robot_port = *(port_t*) response.get_data(0,0);
    position_t remote_robot_position = *(position_t*) response.get_data(0,1);
    UA_Boolean result = *(position_t*) response.get_data(0,2);

    remote_robot* robot = static_cast<remote_robot*>(_userdata);
    // Sanity check
    if(robot->get_port() != remote_robot_port || robot->get_position() != remote_robot_position) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Mismatch on <port,position>. Received<%d,%d>, actually<%d,%d>", __FUNCTION__, remote_robot_port, remote_robot_position, robot->get_port(), robot->get_position());
    }
    if (!result)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d with port %d returned false", __FUNCTION__, robot->get_position(), robot->get_port());
}

void
controller::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
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