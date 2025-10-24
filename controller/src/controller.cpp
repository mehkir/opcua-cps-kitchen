#include "../include/controller.hpp"
#include <open62541/server_config_default.h>
#include <string>
#include <chrono>
#include "filtered_logger.hpp"

#define INSTANCE_NAME "KitchenController"

controller::controller(std::unique_ptr<mape> _kitchen_mape) : server_(UA_Server_new()), controller_type_inserter_(server_, CONTROLLER_TYPE), running_(true), work_guard_(boost::asio::make_work_guard(io_context_)), recipe_parser_(), kitchen_mape_(std::move(_kitchen_mape)) {
    /* Setup controller */
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the controller server");
        running_.store(false);
        return;
    }
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC("urn:kitchen:controller");
    *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add choose next robot method node */
    method_arguments choose_next_robot_arguments;
    choose_next_robot_arguments.add_input_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_input_argument("the processed steps", "processed_steps", UA_TYPES_UINT32);
    choose_next_robot_arguments.add_output_argument("the result", "result", UA_TYPES_BOOLEAN);
    status = controller_type_inserter_.add_method(CONTROLLER_TYPE, CHOOSE_NEXT_ROBOT, choose_next_robot, choose_next_robot_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, CHOOSE_NEXT_ROBOT);
        running_.store(false);
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
        running_.store(false);
        return;
    }
    /* Add controller attributes */
    controller_type_inserter_.add_attribute(CONTROLLER_TYPE, REGISTERED_ROBOTS);
    /* Add controller type constructor */
    controller_type_inserter_.add_object_type_constructor(server_, controller_type_inserter_.get_object_type_id(CONTROLLER_TYPE));
    /* Instantiate controller type */
    controller_type_inserter_.add_object_instance(INSTANCE_NAME, CONTROLLER_TYPE);
    UA_UInt32 initial_registered_robots = 0;
    controller_type_inserter_.set_scalar_attribute(INSTANCE_NAME, REGISTERED_ROBOTS, &initial_registered_robots, UA_TYPES_UINT32);
    /* Run the controller server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error at controller startup");
        running_.store(false);
        return;
    }
    /* Register at discovery server repeatedly */
    if (discovery_util_.register_server_repeatedly(server_) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to start discovery register", __FUNCTION__);
        stop();
        return;
    }
    /* Start the controller event loop */
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_.load()) {
                UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running controller");
        running_.store(false);
        return;
    }
    kitchen_mape_->set_swap_robot_positions_callback(std::bind(&controller::swap_robot_positions, this,
                                                        std::placeholders::_1,
                                                        std::placeholders::_2));
    kitchen_mape_->set_reconfigure_robot_callback(std::bind(&controller::reconfigure_robot_capability, this,
                                                        std::placeholders::_1,
                                                        std::placeholders::_2));
}

controller::~controller() {
    stop();
    join_threads();
    position_remote_robot_map_.clear();
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Destructor finished successfully", __FUNCTION__);
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
    
    if(!UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_STRING])
      || !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasArrayType(&_input[2], &UA_TYPES[UA_TYPES_STRING])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(_method_context);
    /* Extract input arguments */
    UA_String endpoint_tmp = *(UA_String*)_input[0].data;
    std::string endpoint((char*) endpoint_tmp.data, endpoint_tmp.length);
    position_t position = *(position_t*)_input[1].data;
    std::unordered_set<std::string> remote_robot_capabilities;
    for (size_t i = 0; i < _input[2].arrayLength; i++) {
        UA_String capability = ((UA_String*)_input[2].data)[i];
        remote_robot_capabilities.insert(std::string((char*) capability.data, capability.length));
    }

    UA_Boolean result = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(_output, &result, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output parameters", __FUNCTION__);
        self->stop();
        return UA_STATUSCODE_BAD;
    }
    
    self->io_context_.post([self, endpoint, position, remote_robot_capabilities] {
        self->handle_robot_registration(endpoint, position, remote_robot_capabilities);
    });
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_robot_registration(std::string _endpoint, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::string capabilites_str = "REGISTRATION: Capabilities of robot at position " + std::to_string(_position) + " [";
    for (std::string capability : _remote_robot_capabilities) {
        capabilites_str += capability + ", ";
    }
    capabilites_str.erase(capabilites_str.end()-2, capabilites_str.end());
    capabilites_str += "]";
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: %s", __FUNCTION__, capabilites_str.c_str());
    remove_marked_robots();
    erase_stale_pending_swap_entries();
    bool robot_registration_success = false;
    swap_key sk = std::make_tuple(0,0);
    if (is_robot_position_swapping(_position, sk)) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Position is currently involved in a swap (%d,%d)", __FUNCTION__, get<0>(sk), get<1>(sk));
    }
    else if (position_remote_robot_map_.find(_position) == position_remote_robot_map_.end()) {
        position_remote_robot_map_[_position] = std::make_unique<remote_robot>(_endpoint, _position, _remote_robot_capabilities,
                                                                                std::bind(&controller::mark_robot_for_removal, this, std::placeholders::_1),
                                                                                std::bind(&controller::position_swapped_callback, this, std::placeholders::_1, std::placeholders::_2),
                                                                                std::bind(&controller::capabilities_reconfigured_callback, this, std::placeholders::_1));
        increment_or_decrement_counter_node(REGISTERED_ROBOTS);
        robot_registration_success = true;
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: There is already a registered robot at position %d", __FUNCTION__, _position);
    }
}

bool
controller::is_robot_position_swapping(position_t _position, swap_key& _out_key) {
    for (auto entry : pending_swaps_) {
        swap_key key = entry.first;
        if (get<0>(key) == _position || get<1>(key) == _position) {
            _out_key = key;
            return true;
        }
    }
    return false;
}

UA_StatusCode
controller::choose_next_robot(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    if(!UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT32])
    || !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract input arguments */
    recipe_id_t recipe_id = *(recipe_id_t*)_input[0].data;
    UA_UInt32 processed_steps = *(UA_UInt32*)_input[1].data;
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    controller* self = static_cast<controller*>(_method_context);
    UA_Boolean result = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(&_output[0], &result, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output parameters", __FUNCTION__);
        self->stop();
        return UA_STATUSCODE_BAD;
    }
    self->io_context_.post([self, recipe_id, processed_steps] {
        self->handle_next_robot_request(recipe_id, processed_steps);
    });
    return UA_STATUSCODE_GOOD;
}

void
controller::handle_next_robot_request(recipe_id_t _recipe_id, UA_UInt32 _processed_steps) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Conveyor requests next robot for recipe id %d processed with %d steps already", _recipe_id, _processed_steps);
    remove_marked_robots();
    erase_stale_pending_swap_entries();
    remote_robot* next_suitable_robot = find_suitable_robot(_recipe_id, _processed_steps);
    UA_String next_suitable_robot_endpoint = UA_STRING_ALLOC("");
    position_t next_suitable_robot_position = 0;
    if (next_suitable_robot != NULL && !next_suitable_robot->is_adaptivity_pending()) {
        UA_String_clear(&next_suitable_robot_endpoint);
        next_suitable_robot_endpoint = UA_STRING_ALLOC(next_suitable_robot->get_endpoint().c_str());
        next_suitable_robot_position = next_suitable_robot->get_position();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Next robot is at position %d (%s)", next_suitable_robot_position, next_suitable_robot->get_endpoint().c_str());
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: No next suitable robot found");
    }
    // TODO call requesters receive method
    UA_String_clear(&next_suitable_robot_endpoint);
}

remote_robot*
controller::find_suitable_robot(recipe_id_t _recipe_id, UA_UInt32 _processed_steps) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::queue<robot_action> recipe_action_queue = recipe_parser_.get_recipe(_recipe_id).get_action_queue();
    for (size_t i = 0; i < _processed_steps; i++) {
        recipe_action_queue.pop();
    }
    return kitchen_mape_->on_new_order(position_remote_robot_map_, recipe_action_queue);
}

void
controller::swap_robot_positions(position_t _from, position_t _to) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "REARRANGING: Initiating swap for the positions (%d,%d)", _from, _to);
    if (_from == _to) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Position swaps to the same position are ignored (%d,%d)", __FUNCTION__, _from, _to);
        return;
    }
    swap_key sk = (_from < _to) ? std::make_tuple(_from, _to) : std::make_tuple(_to, _from);
    if (pending_swaps_.find(sk) != pending_swaps_.end() || is_robot_position_swapping(_from, sk) || is_robot_position_swapping(_to, sk)) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: There is already an ongoing swap for the positions (%d,%d)", __FUNCTION__, _from, _to);
        return;
    }
    if (position_remote_robot_map_.find(_from) == position_remote_robot_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: There is no robot at position %d", __FUNCTION__, _from);
        return;
    }
    if (position_remote_robot_map_[_from]->is_adaptivity_pending() || !position_remote_robot_map_[_from]->is_available()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d has a pending adaptivity", __FUNCTION__, _from);
        return;
    }
    if (position_remote_robot_map_.find(_to) != position_remote_robot_map_.end() && (position_remote_robot_map_[_to]->is_adaptivity_pending() || !position_remote_robot_map_[_to]->is_available())) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d has a pending adaptivity", __FUNCTION__, _to);
        return;
    }
    size_t output_size = 0;
    UA_Variant* output = nullptr;
    // first robot switch position call
    remote_robot* first_robot = position_remote_robot_map_.at(_from).get();
    UA_StatusCode status = first_robot->switch_position_to(_to, &output_size, &output);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed calling %s method for remote robot at position %d (%s)", __FUNCTION__, SWITCH_POSITION, _from, UA_StatusCode_name(status));
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return;
    }
    UA_Boolean first_will_switch = adaptivity_action_called(output_size, output);
    if (!first_will_switch) {
        // Inconsistency check: This branch should actually never be entered, since availability check above would return early
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d will not switch position", __FUNCTION__, _from);
        stop();
        return;
    }
    first_robot->set_adaptivity_flag();
    // second robot switch position call
    swap_state swap_states;
    output_size = 0;
    output = nullptr;
    if (position_remote_robot_map_.find(_to) != position_remote_robot_map_.end()) {
        remote_robot* second_robot = position_remote_robot_map_.at(_to).get();
        status = second_robot->switch_position_to(_from, &output_size, &output);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed calling %s method for remote robot at position %d (%s)", __FUNCTION__, SWITCH_POSITION, _to, UA_StatusCode_name(status));
            if (output != nullptr) {
                UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                swap_states.second_robot_failed = true;
            }
        } else {
            UA_Boolean second_will_switch = adaptivity_action_called(output_size, output);
            if (!second_will_switch) {
                // Inconsistency check: This branch should actually never be entered, since availability check above would return early
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d will not switch position", __FUNCTION__, _to);
                swap_states.second_robot_failed = true;
                stop();
                return;
            }
        }
        second_robot->set_adaptivity_flag();
    } else {
        /* there is no other robot at the target position (note: this simulates as if the robot at the target position has acked his position switch.
        i.e. the acks signalize based on robots original positions a successful switch) 
        */
        if (_to > _from)
            swap_states.ack_from_greater_position = true;
        else
            swap_states.ack_from_lower_position = true;
    }
    // Add entry
    auto key = (_from < _to) ? std::make_tuple(_from, _to) : std::make_tuple(_to, _from);
    pending_swaps_[key] = swap_states;
}

bool
controller::adaptivity_action_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_output_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return false;
    }
    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return false;
    }
    UA_Boolean will_adapt = *(UA_Boolean*) _output[0].data;
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
    return will_adapt;
}

void
controller::position_swapped_callback(position_t _old_position, position_t _new_position) {
    constexpr const char* func_name = __FUNCTION__;
    io_context_.post([this, _old_position, _new_position, func_name] {
        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", func_name);
        remove_marked_robots();
        erase_stale_pending_swap_entries();
        swap_key sk = (_old_position < _new_position) ? std::make_tuple(_old_position, _new_position) : std::make_tuple(_new_position, _old_position);
        if (pending_swaps_.find(sk) == pending_swaps_.end()) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: There is no pending swap entry for position %d", __FUNCTION__, _new_position);
            stop();
            return;
        }
        swap_state& swap_states = pending_swaps_.at(sk);
        if (swap_states.second_robot_failed) {
            if (position_remote_robot_map_.find(_new_position) != position_remote_robot_map_.end()) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Second robot failed at swap call but is still registered ... Robot at position %d will be removed anyway", __FUNCTION__, _new_position);
                position_remote_robot_map_.erase(_new_position);
                robots_to_be_removed_.erase(_new_position);
                increment_or_decrement_counter_node(REGISTERED_ROBOTS, false);
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Removed remote robot at position %d", _new_position);
            }
            swap_states.second_robot_failed = false;
        }
        if (_old_position == std::get<1>(sk))
            swap_states.ack_from_greater_position = true;
        else
            swap_states.ack_from_lower_position = true;
        // If there is no robot at the target position anymore, then set ack true
        if (position_remote_robot_map_.find(_new_position) == position_remote_robot_map_.end()) {
            if (_new_position == std::get<1>(sk))
                swap_states.ack_from_greater_position = true;
            else
                swap_states.ack_from_lower_position = true;
        }
        if (swap_states.ack_from_lower_position && swap_states.ack_from_greater_position) {
            std::unique_ptr<remote_robot> first = nullptr;
            std::unique_ptr<remote_robot> second = nullptr;
            if (position_remote_robot_map_.find(std::get<0>(sk)) != position_remote_robot_map_.end()) {
                first = std::move(position_remote_robot_map_[std::get<0>(sk)]);
                position_remote_robot_map_.erase(std::get<0>(sk));
            }
            if (position_remote_robot_map_.find(std::get<1>(sk)) != position_remote_robot_map_.end()) {
                second = std::move(position_remote_robot_map_[std::get<1>(sk)]);
                position_remote_robot_map_.erase(std::get<1>(sk));
            }
            if (first != nullptr) {
                first->reset_adaptivity_flag();
                position_remote_robot_map_[first->get_position()] = std::move(first);
            }
            if (second != nullptr) {
                second->reset_adaptivity_flag();
                position_remote_robot_map_[second->get_position()] = std::move(second);
            }
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "REARRANGING: Position swap successfully completed for (%d,%d)", std::get<0>(sk), std::get<1>(sk));
            pending_swaps_.erase(sk);
        }
    });
}

void
controller::erase_stale_pending_swap_entries() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (auto pending_entry = pending_swaps_.begin(); pending_entry != pending_swaps_.end();) {
        swap_key key = pending_entry->first;
        if (position_remote_robot_map_.find(get<0>(key)) == position_remote_robot_map_.end()
            && position_remote_robot_map_.find(get<1>(key)) == position_remote_robot_map_.end()) {
            pending_entry = pending_swaps_.erase(pending_entry);
        } else {
            pending_entry++;
        }
    }
}

void
controller::reconfigure_robot_capability(position_t _robot_position, std::string _new_capabilities_profile) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (position_remote_robot_map_.find(_robot_position) == position_remote_robot_map_.end()) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: There is no robot at position %d", __FUNCTION__, _robot_position);
        return;
    }
    if ((position_remote_robot_map_[_robot_position]->is_adaptivity_pending() || !position_remote_robot_map_[_robot_position]->is_available())) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d has a pending adaptivity", __FUNCTION__, _robot_position);
        return;
    }
    size_t output_size = 0;
    UA_Variant* output = nullptr;
    UA_StatusCode status = position_remote_robot_map_[_robot_position]->reconfigure_capabilities(_new_capabilities_profile, &output_size, &output);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed calling %s method for remote robot at position %d (%s)", __FUNCTION__, RECONFIGURE, _robot_position, UA_StatusCode_name(status));
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return;
    }
    UA_Boolean robot_will_reconfigure = adaptivity_action_called(output_size, output);
    if (!robot_will_reconfigure) {
        // Inconsistency check: This branch should actually never be entered, since availability check above would return early
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d will not reconfigure", __FUNCTION__, _robot_position);
        stop();
        return;
    }
    position_remote_robot_map_[_robot_position]->set_adaptivity_flag();
}

void
controller::capabilities_reconfigured_callback(position_t _robot_position) {
    constexpr const char* func_name = __FUNCTION__;
    io_context_.post([this, _robot_position, func_name] {
        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", func_name);
        remove_marked_robots();
        if (position_remote_robot_map_.find(_robot_position) != position_remote_robot_map_.end()) {
            position_remote_robot_map_[_robot_position]->reset_adaptivity_flag();
        }
    });
}

void
controller::mark_robot_for_removal(position_t _position) {
    constexpr const char* func_name = __FUNCTION__;
    io_context_.post([this, _position, func_name] {
        // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", func_name);
        robots_to_be_removed_.insert(_position);
    });
}

void
controller::remove_marked_robots() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (position_t position : robots_to_be_removed_) {
        if (position_remote_robot_map_.find(position) != position_remote_robot_map_.end()) {
            position_remote_robot_map_.erase(position);
            increment_or_decrement_counter_node(REGISTERED_ROBOTS, false);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Removed remote robot at position %d", position);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "No remote robot found at position %d", position);
        }
    }
}

UA_StatusCode
controller::increment_or_decrement_counter_node(std::string _attribute_name, bool increment) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Variant value;
    UA_Variant_init(&value);
    if ((status = controller_type_inserter_.get_attribute(INSTANCE_NAME, _attribute_name.c_str(), value)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error getting attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
        UA_Variant_clear(&value);
        return status;
    }
    UA_UInt32 counter_value = 0;
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32]) && value.data) {
        counter_value = *(UA_UInt32*) value.data;
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Unexpected attribute type for %s", __FUNCTION__, _attribute_name.c_str());
        UA_Variant_clear(&value);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    if (increment)
        counter_value++;
    else
        counter_value--;
    if ((status = controller_type_inserter_.set_scalar_attribute(INSTANCE_NAME, _attribute_name.c_str(), &counter_value, UA_TYPES_UINT32)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
        UA_Variant_clear(&value);
        return status;
    }
    UA_Variant_clear(&value);
    return status;
}

void
controller::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if (worker_thread_.joinable())
        worker_thread_.join();
}

void
controller::start() {
    if (!running_.load())
        stop();
    worker_thread_ = std::thread([this]() {
        io_context_.run();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited io_context", __FUNCTION__);
    });
    join_threads();
}

void
controller::stop() {
    running_.store(false);
    work_guard_.reset();
    io_context_.stop();
    discovery_util_.stop();
    discovery_util_.deregister_server(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Stop finished successfully", __FUNCTION__);
}