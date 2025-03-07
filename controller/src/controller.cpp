#include "../include/controller.hpp"
#include <open62541/server_config_default.h>
#include <string>
#include <response_checker.hpp>
#include <random>

#define RECIPE_PATH "recipes.json"

controller::controller(port_t _port) : server_(UA_Server_new()), port_(_port), running_(true), recipe_parser_(RECIPE_PATH) {
    /* Setup controller */
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the controller server");
        running_ = false;
        return;
    }

    receive_robot_state_inserter_.add_input_argument("robot port", "robot_port", UA_TYPES_UINT16);
    receive_robot_state_inserter_.add_input_argument("robot position", "robot_position", UA_TYPES_UINT32);
    receive_robot_state_inserter_.add_input_argument("robot state", "robot_state", UA_TYPES_UINT32);
    receive_robot_state_inserter_.add_input_argument("robot current tool", "robot_current_tool", UA_TYPES_UINT32);
    receive_robot_state_inserter_.add_input_argument("session id", "session_id", UA_TYPES_UINT32);
    receive_robot_state_inserter_.add_input_argument("message counter", "message_counter", UA_TYPES_UINT32);
    receive_robot_state_inserter_.add_output_argument("robot state received", "robot_state_received", UA_TYPES_BOOLEAN);
    status = receive_robot_state_inserter_.add_method_node(server_, UA_NODEID_STRING(1, const_cast<char*>(RECEIVE_ROBOT_STATE)), "receive robot state", receive_robot_state, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the receive robot state method node");
        running_ = false;
        return;
    }

    register_robot_inserter_.add_input_argument("robot port", "robot_port", UA_TYPES_UINT16);
    register_robot_inserter_.add_input_argument("robot position", "robot_position", UA_TYPES_UINT32);
    register_robot_inserter_.add_input_argument("robot capabilities", "robot_capabilities", UA_TYPES_STRING);
    register_robot_inserter_.add_output_argument("capabilities received", "capabilities_received", UA_TYPES_BOOLEAN);
    status = register_robot_inserter_.add_method_node(server_, UA_NODEID_STRING(1, const_cast<char*>(REGISTER_ROBOT)), "register robot", register_robot, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error adding the register robot method node");
        running_ = false;
        return;
    }

    /* Run the controller server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error at controller startup");
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
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running controller");
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
    for (size_t i = 0; i <= _input[2].arrayLength; i++) {
        UA_String capability = ((UA_String*)_input[2].data)[i];
        remote_robot_capabilities.insert(std::string((char*) capability.data, capability.length));
    }
    /* Extract method context */
    controller* self = static_cast<controller*>(_method_context);
    self->handle_robot_registration(port, position, remote_robot_capabilities, _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_robot_registration(port_t _port, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (position_remote_robot_map_.find(_position) == position_remote_robot_map_.end()) {
        position_remote_robot_map_[_position] = std::make_unique<remote_robot>(_port, _position, _remote_robot_capabilities);
    }
    bool capabilities_received = true;
    UA_Variant_setScalarCopy(_output, &capabilities_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
}

UA_StatusCode
controller::receive_robot_state(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 6) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    UA_StatusCode status = !UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT16]);
    status |= !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[2], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[3], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[4], &UA_TYPES[UA_TYPES_UINT32]);
    status |= !UA_Variant_hasScalarType(&_input[5], &UA_TYPES[UA_TYPES_UINT32]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract input arguments */
    port_t port = *(port_t*)_input[0].data;
    position_t position = *(position_t*)_input[1].data;
    robot_state remote_robot_state = *(robot_state*)_input[2].data;
    robot_tool current_remote_robot_tool = *(robot_tool*)_input[3].data;
    session_id_t id = *(session_id_t*)_input[4].data;
    message_counter_t message_counter = *(message_counter_t*)_input[5].data;
    /* Extract method context */
    controller* self = static_cast<controller*>(_method_context);
    self->handle_robot_state(port, position, remote_robot_state, current_remote_robot_tool, session_id(id, message_counter), _output);
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_robot_state(port_t _port, position_t _position, robot_state _remote_robot_state, robot_tool _current_remote_robot_tool, session_id _session_id, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (position_remote_robot_map_.find(_position) == position_remote_robot_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d must register first", __FUNCTION__, _position);
        return;
    }
    remote_robot& robot = position_remote_robot_map_[_position].operator*();
    if (robot.get_session_id() > _session_id) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad session id, this should not happen", __FUNCTION__);
        running_ = false;
        return;
    }
    robot.set_state(_remote_robot_state);
    robot.set_current_tool(_current_remote_robot_tool);
    robot.set_state_status(remote_robot::state_status::CURRENT);
    robot.get_session_id().id_ = _session_id.id_;
    robot.get_session_id().message_counter_ = _session_id.message_counter_;

    std::random_device random_device;
    std::mt19937 mersenne_twister(random_device());
    std::uniform_int_distribution<std::uint32_t> distribution(1, 3);
    cps_kitchen::recipe_id_t recipe_id = distribution(mersenne_twister);
    std::string first_action = recipe_parser_.get_recipe(recipe_id).get_action_queue().front().get_name();

    for (auto position_remote_robot = position_remote_robot_map_.begin(); position_remote_robot != position_remote_robot_map_.end(); position_remote_robot++) {
        remote_robot& robot = position_remote_robot->second.operator*();
        if (robot.get_state_status() == remote_robot::state_status::CURRENT && robot.get_state() == robot_state::IDLING && robot.is_capable_to(first_action)) {
            robot.set_state_status(remote_robot::state_status::OBSOLETE);
            robot.get_session_id().increment_id();
            robot.instruct(distribution(mersenne_twister), receive_robot_task_called);
            break;
        }
    }
    UA_Boolean robot_state_received = true;
    UA_Variant_setScalarCopy(_output, &robot_state_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
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

    if(response.get_output_arguments_size(0) != 5) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return;
    }

    if(!response.has_scalar_type(0, 0, &UA_TYPES[UA_TYPES_UINT16])
      || !response.has_scalar_type(0, 1, &UA_TYPES[UA_TYPES_UINT32])
      || !response.has_scalar_type(0, 2, &UA_TYPES[UA_TYPES_UINT32])
      || !response.has_scalar_type(0, 3, &UA_TYPES[UA_TYPES_UINT32])
      || !response.has_scalar_type(0, 4, &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }

    port_t remote_robot_port = *(port_t*) response.get_data(0,0);
    position_t remote_robot_position = *(position_t*) response.get_data(0,1);
    robot_state remote_robot_state = *(robot_state*) response.get_data(0,2);
    session_id_t id = *(session_id_t*) response.get_data(0,3);
    message_counter_t message_counter = *(message_counter_t*) response.get_data(0,4);

    remote_robot* robot = static_cast<remote_robot*>(_userdata);
    // Sanity check
    if(robot->get_port() != remote_robot_port || robot->get_position() != remote_robot_position) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "%s: Mismatch on <port,position>. Received<%d,%d>, actually<%d,%d>", __FUNCTION__, remote_robot_port, remote_robot_position, robot->get_port(), robot->get_position());
    }

    if (session_id(id, message_counter) < robot->get_session_id()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Received state lies behind and is therefore ignored", __FUNCTION__);
        return;
    }
    robot->get_session_id().id_ = id;
    robot->get_session_id().message_counter_ = message_counter;
    robot->set_state(remote_robot_state);
    robot->set_state_status(remote_robot::state_status::CURRENT);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "ROBOT_STATE (after): Position=%d, state=%s, state status=%s", robot->get_position(), robot_state_to_string(robot->get_state()).c_str(), remote_robot::remote_robot_state_status_to_string(robot->get_state_status()).c_str());
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