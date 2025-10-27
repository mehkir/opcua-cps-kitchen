#include "../include/kitchen.hpp"

#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include "filtered_logger.hpp"
#include "discovery_and_connection.hpp"
#include "information_node_reader.hpp"
#include "time_unit.hpp"

#define INSTANCE_NAME "CpsKitchen"
#define REMOTE_CONTROLLER_INSTANCE_NAME "RemoteKitchenController"
#define REMOTE_CONVEYOR_INSTANCE_NAME "RemoteKitchenConveyor"
#define PlACING_RATE 5LL

kitchen::kitchen(uint32_t _robot_count) : server_(UA_Server_new()), kitchen_uri_("urn:kitchen:env"), kitchen_type_inserter_(server_, KITCHEN_TYPE), running_(true), remote_robot_type_inserter_(server_, REMOTE_ROBOT_TYPE),
                                        robot_count_(_robot_count), remote_controller_type_inserter_(server_, REMOTE_CONTROLLER_TYPE), remote_conveyor_type_inserter_(server_, REMOTE_CONVEYOR_TYPE), recipe_parser_(),
                                        mersenne_twister_(random_device_()), uniform_int_distribution_(1,recipe_parser_.get_recipe_count()), controller_client_(nullptr), conveyor_client_(nullptr),
                                        work_guard_(boost::asio::make_work_guard(io_context_)), placing_timer_(io_context_), placing_gate_open_(true) {
    /* Setup kitchen environment */
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error with setting up the server", __FUNCTION__);
        return;
    }
    // Set a unique application URI for the robot
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC(kitchen_uri_.c_str());
    *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add kitchen attributes */
    kitchen_type_inserter_.add_attribute(KITCHEN_TYPE, ASSIGNED_ORDERS);
    kitchen_type_inserter_.add_attribute(KITCHEN_TYPE, DROPPED_ORDERS);
    kitchen_type_inserter_.add_attribute(KITCHEN_TYPE, RECEIVED_ORDERS);
    kitchen_type_inserter_.add_attribute(KITCHEN_TYPE, COMPLETED_ORDERS);
    /* Add place random order method node */
    method_arguments place_random_order_arguments;
    place_random_order_arguments.add_output_argument("indicates whether the kitchen received the order", "order_received", UA_TYPES_BOOLEAN);
    status = kitchen_type_inserter_.add_method(KITCHEN_TYPE, PLACE_RANDOM_ORDER, place_random_order, place_random_order_arguments, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, PLACE_RANDOM_ORDER);
        return;
    }
    /* Add receive next robot method node */
    method_arguments receive_next_robot_arguments;
    receive_next_robot_arguments.add_input_argument("the remote robot's position", "robot_position", UA_TYPES_UINT32);
    receive_next_robot_arguments.add_input_argument("the remote robot's endpoint", "robot_endpoint", UA_TYPES_STRING);
    receive_next_robot_arguments.add_input_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    receive_next_robot_arguments.add_output_argument("confirms the next robot receival", "result", UA_TYPES_BOOLEAN);
    status = kitchen_type_inserter_.add_method(KITCHEN_TYPE, RECEIVE_NEXT_ROBOT, receive_next_robot, receive_next_robot_arguments, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, RECEIVE_NEXT_ROBOT);
        return;
    }
    /* Add receive completed order method node */
    method_arguments receive_completed_order_arguments;
    receive_completed_order_arguments.add_input_argument("recipe id of completed order", "recipe_id", UA_TYPES_UINT32);
    receive_completed_order_arguments.add_output_argument("indicates whether the completed order is received", "completed_order_received", UA_TYPES_BOOLEAN);
    status = kitchen_type_inserter_.add_method(KITCHEN_TYPE, RECEIVE_COMPLETED_ORDER, receive_completed_order, receive_completed_order_arguments, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, RECEIVE_COMPLETED_ORDER);
        return;
    }
    /* Add kitchen type constructor */
    kitchen_type_inserter_.add_object_type_constructor(server_, kitchen_type_inserter_.get_object_type_id(KITCHEN_TYPE));
    /* Instantiate kitchen type */
    kitchen_type_inserter_.add_object_instance(INSTANCE_NAME, KITCHEN_TYPE);
    UA_UInt32 initial_orders_count = 0;
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, ASSIGNED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, DROPPED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, RECEIVED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, COMPLETED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    /* Add the remote controller type */
    UA_Boolean initial_connectivity_state = false;
    remote_controller_type_inserter_.add_attribute(REMOTE_CONTROLLER_TYPE, CONNECTIVITY);
    remote_controller_type_inserter_.add_object_type_constructor(server_, remote_controller_type_inserter_.get_object_type_id(REMOTE_CONTROLLER_TYPE));
    remote_controller_type_inserter_.add_object_instance(REMOTE_CONTROLLER_INSTANCE_NAME, REMOTE_CONTROLLER_TYPE, kitchen_type_inserter_.get_instance_id(INSTANCE_NAME), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
    remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &initial_connectivity_state, UA_TYPES_BOOLEAN);
    /* Add the remote conveyor type */
    remote_conveyor_type_inserter_.add_attribute(REMOTE_CONVEYOR_TYPE, CONNECTIVITY);
    remote_conveyor_type_inserter_.add_object_type_constructor(server_, remote_conveyor_type_inserter_.get_object_type_id(REMOTE_CONVEYOR_TYPE));
    remote_conveyor_type_inserter_.add_object_instance(REMOTE_CONVEYOR_INSTANCE_NAME, REMOTE_CONVEYOR_TYPE, kitchen_type_inserter_.get_instance_id(INSTANCE_NAME), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
    remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &initial_connectivity_state, UA_TYPES_BOOLEAN);
    /* Add remote robot type constructor */
    if (remote_robot::setup_remote_robot_object_type(remote_robot_type_inserter_, server_) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the remote robot type constructor", __FUNCTION__);
        stop();
        return;
    }
    /* Add remote robot instances to the address space */
    for (position_t position = 1; position <= robot_count_; position++) {
        status = remote_robot_type_inserter_.add_object_instance(remote_robot::remote_robot_instance_name(position).c_str(), REMOTE_ROBOT_TYPE, kitchen_type_inserter_.get_instance_id(INSTANCE_NAME), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
        status |= remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(position), POSITION, &position, UA_TYPES_UINT32);
        status |= remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(position), CONNECTIVITY, &initial_connectivity_state, UA_TYPES_BOOLEAN);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding remote robot object and setting initial attributes (%s)", __FUNCTION__, UA_StatusCode_name(status));
            stop();
            return;
        }
    }
    /* Run the kitchen server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error at kitchen startup");
        return;
    }
    /* Register at discovery server repeatedly */
    if (discovery_util_.register_server_repeatedly(server_) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to start discovery register", __FUNCTION__);
        stop();
        return;
    }
    /* Start the kitchen event loop */
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_.load()) {
                UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running kitchen");
        running_.store(false);
        return;
    }
    /* Setup controller client */
    std::string controller_endpoint;
    while((status = discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to controller, retrying in %d seconds (%s)", __FUNCTION__, LOOKUP_INTERVAL, UA_StatusCode_name(status));
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        if (!running_.load()) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to controller", __FUNCTION__);
            stop();
            return;
        }
    }
    UA_Boolean connectivity_state = true;
    remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
    /* Gather method ids */
    if ((method_id_map_[CHOOSE_NEXT_ROBOT] = node_browser_helper().get_method_id(controller_endpoint, CONTROLLER_TYPE, CHOOSE_NEXT_ROBOT)) == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, CHOOSE_NEXT_ROBOT);
        stop();
        return;        
    }
    /* Setup conveyor client */
    std::string conveyor_endpoint;
    while((status = discover_and_connect(conveyor_client_, discovery_util_, conveyor_endpoint, CONVEYOR_TYPE)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to conveyor, retrying in %d seconds (%s)", __FUNCTION__, LOOKUP_INTERVAL, UA_StatusCode_name(status));
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        if (!running_.load()) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to conveyor", __FUNCTION__);
            stop();
            return;
        }
    }
    remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
}

kitchen::~kitchen() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    stop();
    join_threads();

    /* Destroy remote robot instances BEFORE deleting the UA server
    to avoid remote_robot::~remote_robot() touching a freed UA_Server */
    {
        std::lock_guard<std::mutex> lock(remote_robot_discovery_mutex_);
        position_remote_robot_map_.clear(); // remote_robot dtors run here
    }

    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (controller_client_ != nullptr)
            UA_Client_delete(controller_client_);
        if (conveyor_client_ != nullptr)
            UA_Client_delete(conveyor_client_);
    }
    UA_String_clear(&server_endpoint_);
    UA_String_clear(&type_);
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Destructor finished successfully", __FUNCTION__);
}

UA_StatusCode
kitchen::receive_completed_order(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    if(_input_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    if (!UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    recipe_id_t completed_recipe = *(recipe_id_t*)_input[0].data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Conveyor delivered completed dish with the recipe ID %d", __FUNCTION__, completed_recipe);
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    kitchen* self = static_cast<kitchen*>(_method_context);
    self->increment_orders_counter(COMPLETED_ORDERS);
    UA_Boolean result = true;
    UA_Variant_setScalarCopy(_output, &result, &UA_TYPES[UA_TYPES_BOOLEAN]);
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode
kitchen::place_random_order(UA_Server* _server,
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
    kitchen* self = static_cast<kitchen*>(_method_context);
    self->io_context_.post([self] {
        self->handle_random_order_request();
    });
    UA_Boolean result = true;
    UA_Variant_setScalarCopy(_output, &result, &UA_TYPES[UA_TYPES_BOOLEAN]);
    return UA_STATUSCODE_GOOD;
}

void
kitchen::handle_random_order_request() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    auto do_place = [this] {
        increment_orders_counter(RECEIVED_ORDERS);
        bool instructed = false;
        recipe_id_t recipe_id = uniform_int_distribution_(mersenne_twister_);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RANDOM ORDER: Generated recipe with the ID %d", recipe_id);
        object_method_info omi = method_id_map_[CHOOSE_NEXT_ROBOT];
        UA_Variant* output = nullptr;
        size_t output_size = 0;
        {
            std::unique_lock<std::mutex> lock(client_mutex_);
            method_node_caller choose_next_robot_caller;
            UA_UInt32 processed_steps = 0;
            choose_next_robot_caller.add_scalar_input_argument(&recipe_id, UA_TYPES_UINT32);
            choose_next_robot_caller.add_scalar_input_argument(&processed_steps, UA_TYPES_UINT32);
            choose_next_robot_caller.add_scalar_input_argument(&server_endpoint_, UA_TYPES_STRING);
            choose_next_robot_caller.add_scalar_input_argument(&type_, UA_TYPES_STRING);
            UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
            while (status != UA_STATUSCODE_GOOD) {
                if (controller_client_ != nullptr)
                    status = choose_next_robot_caller.call_method_node(controller_client_, omi.object_id_, omi.method_id_, &output_size, &output);
                if (running_.load() && status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling choose next robot (%s)", __FUNCTION__, UA_StatusCode_name(status));
                    if (output != nullptr ) {
                        UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                        output = nullptr;
                        output_size = 0;
                    }
                    UA_Client_delete(controller_client_);
                    controller_client_ = nullptr;
                    remote_controller_connected_cv_.wait(lock);
                }
                if (!running_.load()) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to call choose next robot", __FUNCTION__);
                    if (output != nullptr )
                        UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                    return;
                }
            }
        }
        bool result = choose_next_robot_called(output_size, output);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RANDOM ORDER: Controller returned %s for next robot request.", result ? "true" : "false");
    };

    if (placing_gate_open_) {
        placing_gate_open_ = false;
        do_place();
        arm_placing_gate();
    } else {
        placing_queue_.push(std::move(do_place));
    }
}

void
kitchen::arm_placing_gate() {
    placing_timer_.expires_after(std::chrono::milliseconds(PlACING_RATE * TIME_UNIT));
    placing_timer_.async_wait([this](const boost::system::error_code& ec){
        if (ec) {
            // timer cancelled on shutdown; ignore
            return;
        }
        if (!placing_queue_.empty()) {
            auto task = std::move(placing_queue_.front());
            placing_queue_.pop();
            task();
            arm_placing_gate();
        } else {
            placing_gate_open_ = true;
        }
    });
}

bool
kitchen::choose_next_robot_called(size_t _output_size, UA_Variant *_output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return false;
    }
    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return false;
    }
    UA_Boolean result = *(UA_Boolean*) _output[0].data;
    return result;
}

UA_StatusCode
kitchen::receive_next_robot(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_input_size != 3) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    if(!UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_UINT32])
    || !UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_STRING])
    || !UA_Variant_hasScalarType(&_input[2], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    kitchen* self = static_cast<kitchen*>(_method_context);
    /* Extract input arguments */
    position_t robot_position = *(position_t*) _input[0].data;
    UA_String robot_endpoint = *(UA_String*) _input[1].data;
    recipe_id_t recipe_id = *(recipe_id_t*) _input[2].data;

    UA_Boolean result = true;
    UA_StatusCode status = UA_Variant_setScalarCopy(_output, &result, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting output parameters", __FUNCTION__);
        self->stop();
        return UA_STATUSCODE_BAD;
    }

    std::string robot_endpoint_str((char*) robot_endpoint.data, robot_endpoint.length);
    self->io_context_.post([self, robot_position, robot_endpoint_str, recipe_id] {
        self->handle_receive_next_robot(robot_position, robot_endpoint_str, recipe_id);
    });
    return UA_STATUSCODE_GOOD;
}

void
kitchen::handle_receive_next_robot(position_t _robot_position, std::string _robot_endpoint, recipe_id_t _recipe_id) {
    remove_marked_robots();
    if (_robot_position == 0 || _robot_endpoint.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: The controller couldn't return a suitable robot. Dropping order with recipe id %d", _recipe_id);
        increment_orders_counter(DROPPED_ORDERS);
        return;
    }
    std::lock_guard<std::mutex> lock(remote_robot_discovery_mutex_);
    if (position_remote_robot_map_.find(_robot_position) == position_remote_robot_map_.end() || _robot_endpoint.compare(position_remote_robot_map_[_robot_position]->get_endpoint())) {
        position_remote_robot_map_.erase(_robot_position);
        robots_to_be_removed_.erase(_robot_position);
        std::unique_ptr<remote_robot> robot = std::make_unique<remote_robot>(_robot_endpoint, _robot_position, remote_robot_type_inserter_,
                                                                            std::bind(&kitchen::mark_robot_for_removal, this, std::placeholders::_1),
                                                                            std::bind(&kitchen::position_swapped_callback, this, std::placeholders::_1, std::placeholders::_2));
        if (robot->initialize_and_start() != UA_STATUSCODE_GOOD) {
            increment_orders_counter(DROPPED_ORDERS);
            return;
        }
        position_remote_robot_map_[_robot_position] = std::move(robot);
    }
    size_t output_size = 0;
    UA_Variant* output = nullptr;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: The controller returned the robot at position %d (%s) for recipe id %d", _robot_position, _robot_endpoint.c_str(), _recipe_id);
    UA_StatusCode status = position_remote_robot_map_[_robot_position]->instruct(_recipe_id, 0, &output_size, &output);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: Failed calling %s method", RECEIVE_TASK);
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        increment_orders_counter(DROPPED_ORDERS);
        return;
    }
    if (receive_robot_task_called(output_size, output)) {
        increment_orders_counter(ASSIGNED_ORDERS);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: Assigned the next robot at position %d (%s) with recipe id %d", _robot_position, _robot_endpoint.c_str(), _recipe_id);
    } else {
        increment_orders_counter(DROPPED_ORDERS);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: Dropped order for the next robot at position %d (%s) with recipe id %d", _robot_position, _robot_endpoint.c_str(), _recipe_id);
    }
}

bool
kitchen::receive_robot_task_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return false;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_UINT32])
       || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return false;
    }

    position_t remote_robot_position = *(position_t*) _output[0].data;
    UA_Boolean result = *(UA_Boolean*) _output[1].data;
    remote_robot* robot = nullptr;
    if (position_remote_robot_map_.find(remote_robot_position) != position_remote_robot_map_.end())
        robot = position_remote_robot_map_[remote_robot_position].get();

    if (robot == nullptr) {
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return false;
    }

    if (!result) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d returned false", __FUNCTION__, robot->get_position());
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return false;
    }
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
    return result;
}

void
kitchen::position_swapped_callback(position_t _old_position, position_t _new_position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::lock_guard<std::mutex> lock(remote_robot_discovery_mutex_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "REARRANGING(Kitchen): Reflecting position swap/switch from %d to %d", _old_position, _new_position);
    remote_robot* first = nullptr;
    remote_robot* second = nullptr;
    if (position_remote_robot_map_.find(_old_position) != position_remote_robot_map_.end()) {
        first = position_remote_robot_map_[_old_position].get();
    }
    if (position_remote_robot_map_.find(_new_position) != position_remote_robot_map_.end()) {
        second = position_remote_robot_map_[_new_position].get();
    }
    if ((first != nullptr && first->get_position() != _old_position)
        || (second != nullptr && second->get_position() != _new_position) ) {
            std::swap(position_remote_robot_map_[_old_position], position_remote_robot_map_[_new_position]);
    }
    UA_Boolean disconnected = false;
    UA_Boolean connected = true;
    if (position_remote_robot_map_[_old_position] == nullptr) {
        position_remote_robot_map_.erase(_old_position);
        robots_to_be_removed_.erase(_old_position);
        remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(_old_position), CONNECTIVITY, &disconnected, UA_TYPES_BOOLEAN);
    } else {
        remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(_old_position), CONNECTIVITY, &connected, UA_TYPES_BOOLEAN);
    }
    if (position_remote_robot_map_[_new_position] == nullptr) {
        position_remote_robot_map_.erase(_new_position);
        robots_to_be_removed_.erase(_new_position);
        remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(_new_position), CONNECTIVITY, &disconnected, UA_TYPES_BOOLEAN);
    } else {
        remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(_new_position), CONNECTIVITY, &connected, UA_TYPES_BOOLEAN);
    }
}

UA_StatusCode
kitchen::increment_orders_counter(std::string _attribute_name) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Variant value;
    UA_Variant_init(&value);
    if ((status = kitchen_type_inserter_.get_attribute(INSTANCE_NAME, _attribute_name.c_str(), value)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error getting attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
        UA_Variant_clear(&value);
        return status;
    }
    UA_UInt32 dishes_counter = 0;
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32]) && value.data) {
        dishes_counter = *(UA_UInt32*) value.data;
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Unexpected attribute type for %s", __FUNCTION__, _attribute_name.c_str());
        UA_Variant_clear(&value);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    dishes_counter++;
    if ((status = kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, _attribute_name.c_str(), &dishes_counter, UA_TYPES_UINT32)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
        UA_Variant_clear(&value);
        return status;
    }
    UA_Variant_clear(&value);
    return status;
}

void
kitchen::mark_robot_for_removal(position_t _position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::lock_guard<std::mutex> lock(remote_robot_discovery_mutex_);
    robots_to_be_removed_.insert(_position);
}

void
kitchen::remove_marked_robots() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::lock_guard<std::mutex> lock(remote_robot_discovery_mutex_);
    for (position_t position : robots_to_be_removed_) {
        if (position_remote_robot_map_.find(position) != position_remote_robot_map_.end()) {
            position_remote_robot_map_.erase(position);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Removed remote robot at position %d", position);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "No remote robot found at position %d", position);
        }
    }
    robots_to_be_removed_.clear();
    if (position_remote_robot_map_.size() < robot_count_)
        remote_robot_discovery_cv.notify_all();
}

void
kitchen::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if (client_iterate_thread_.joinable())
        client_iterate_thread_.join();
    if (cyclic_remote_robot_discovery_thread_.joinable())
        cyclic_remote_robot_discovery_thread_.join();
    if(worker_thread_.joinable())
        worker_thread_.join();
}

void
kitchen::start() {
    if (!running_.load()) {
        stop();
        return;
    }
    /* Lookup own endpoint */
    std::vector<std::string> endpoints;
    while (endpoints.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Looking up own endpoint", __FUNCTION__);
        if (discovery_util_.lookup_endpoints(endpoints, kitchen_uri_) != UA_STATUSCODE_GOOD || endpoints.empty()) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Couldn't look up own endpoint. Trying again in %d seconds", __FUNCTION__, LOOKUP_INTERVAL);
            std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        }
        if (!running_.load()) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error looking up own endpoint url", __FUNCTION__);
            stop();
            return;
        }
    }
    UA_String_init(&server_endpoint_);
    server_endpoint_ = UA_STRING_ALLOC(const_cast<char*>(endpoints[0].c_str()));
    /* Set type member variable */
    UA_String_init(&type_);
    type_ = UA_STRING_ALLOC(const_cast<char*>(KITCHEN_TYPE));
    /* Run the client iterate thread */
    try {
        client_iterate_thread_ = std::thread([this]() {
            while(running_.load()) {
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    if (controller_client_ != nullptr) {
                        UA_StatusCode status = UA_Client_run_iterate(controller_client_, 1);
                        if (status != UA_STATUSCODE_GOOD) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running controller client iterate", __FUNCTION__);
                            UA_Client_delete(controller_client_);
                            controller_client_ = nullptr;
                            UA_Boolean connectivity_state = false;
                            remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                        }
                    } else {
                        std::string controller_endpoint;
                        if (discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE) != UA_STATUSCODE_GOOD)
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error reconnecting to controller. Retrying ...", __FUNCTION__);
                        else {
                            UA_Boolean connectivity_state = true;
                            remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                            remote_controller_connected_cv_.notify_all();
                        }
                    }

                    if (conveyor_client_ != nullptr) {
                        UA_StatusCode status = UA_Client_run_iterate(conveyor_client_, 1);
                        if (status != UA_STATUSCODE_GOOD) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running conveyor client iterate", __FUNCTION__);
                            UA_Client_delete(conveyor_client_);
                            conveyor_client_ = nullptr;
                            UA_Boolean connectivity_state = false;
                            remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                        }
                    } else {
                        std::string conveyor_endpoint;
                        if (discover_and_connect(conveyor_client_, discovery_util_, conveyor_endpoint, CONVEYOR_TYPE) != UA_STATUSCODE_GOOD)
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error reconnecting to conveyor. Retrying ...", __FUNCTION__);
                        else {
                            UA_Boolean connectivity_state = true;
                            remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                        }
                    }
                }
                remove_marked_robots();
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
    /* Run the cyclic remote robot discovery thread */
    try {
        cyclic_remote_robot_discovery_thread_ = std::thread([this]() {
            while (running_.load()) {
                {
                    std::unique_lock<std::mutex> lock(remote_robot_discovery_mutex_);
                    std::vector<std::string> endpoints;
                    if (discovery_util_.lookup_endpoints(endpoints) != UA_STATUSCODE_GOOD) {
                        continue;
                    }
                    for (std::string endpoint : endpoints) {
                        if (node_browser_helper().has_instance(endpoint, ROBOT_TYPE)) {
                            /* Get position remote robot's position */
                            UA_Client* remote_robot_client = nullptr;
                            client_connection_establisher cce;
                            bool connected = cce.establish_connection(remote_robot_client, endpoint);
                            if (!connected) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error establishing robot client session", __FUNCTION__);
                                if (remote_robot_client != nullptr)
                                    UA_Client_delete(remote_robot_client);
                                continue;
                            }
                            UA_NodeId position_node_id = node_browser_helper().get_attribute_id(remote_robot_client, ROBOT_TYPE, POSITION);
                            if (UA_NodeId_equal(&position_node_id, &UA_NODEID_NULL)) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
                                UA_Client_delete(remote_robot_client);
                                continue;
                            }
                            information_node_reader inr;
                            if (inr.read_information_node(remote_robot_client, position_node_id) != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not read the %s attribute id", __FUNCTION__, POSITION);
                                UA_Client_delete(remote_robot_client);
                                continue;
                            }
                            position_t remote_robot_position = *(position_t*)inr.get_variant()->data;
                            UA_NodeId availability_node_id = node_browser_helper().get_attribute_id(remote_robot_client, ROBOT_TYPE, AVAILABILITY);
                            if (UA_NodeId_equal(&availability_node_id, &UA_NODEID_NULL)) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, AVAILABILITY);
                                UA_Client_delete(remote_robot_client);
                                continue;
                            }
                            if (inr.read_information_node(remote_robot_client, availability_node_id) != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not read the %s attribute id", __FUNCTION__, AVAILABILITY);
                                UA_Client_delete(remote_robot_client);
                                continue;
                            }
                            UA_Boolean available = *(UA_Boolean*)inr.get_variant()->data;
                            if (available && (position_remote_robot_map_.find(remote_robot_position) == position_remote_robot_map_.end() || endpoint.compare(position_remote_robot_map_[remote_robot_position]->get_endpoint()))) {
                                position_remote_robot_map_.erase(remote_robot_position);
                                robots_to_be_removed_.erase(remote_robot_position);
                                std::unique_ptr<remote_robot> robot = std::make_unique<remote_robot>(endpoint, remote_robot_position, remote_robot_type_inserter_,
                                                                                                                std::bind(&kitchen::mark_robot_for_removal, this, std::placeholders::_1),
                                                                                                                std::bind(&kitchen::position_swapped_callback, this, std::placeholders::_1, std::placeholders::_2));
                                if (robot->initialize_and_start() == UA_STATUSCODE_GOOD)
                                    position_remote_robot_map_[remote_robot_position] = std::move(robot);
                            }
                            UA_Client_delete(remote_robot_client);
                        }
                    }
                    if (position_remote_robot_map_.size() == robot_count_) {
                        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Discovered all remote robots, wait for notification ...", __FUNCTION__);
                        remote_robot_discovery_cv.wait(lock);
                        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Received notification, rediscovering remote robots.", __FUNCTION__);
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }
    catch(...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the remote robot discovery thread");
        stop();
        return;
    }
    /* Setup worker thread */
    worker_thread_ = std::thread([this]() {
        io_context_.run();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited io_context", __FUNCTION__);
    });
    join_threads();
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited start method", __FUNCTION__);
}

void
kitchen::stop() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    {
        std::lock_guard<std::mutex> remote_robot_lock(remote_robot_discovery_mutex_);
        std::lock_guard<std::mutex> client_loop_lock(client_mutex_);
        running_.store(false);
        remote_robot_discovery_cv.notify_all();
        remote_controller_connected_cv_.notify_all();
    }
    work_guard_.reset();
    io_context_.stop();
    discovery_util_.stop();
    discovery_util_.deregister_server(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Stop finished successfully", __FUNCTION__);
}