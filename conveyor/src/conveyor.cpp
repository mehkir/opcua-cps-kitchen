#include "../include/conveyor.hpp"
#include <open62541/server_config_default.h>

#include <string>
#include <memory>
#include "callback_scheduler.hpp"
#include "time_unit.hpp"
#include "filtered_logger.hpp"
#include "discovery_and_connection.hpp"

#define CONVEYOR_INSTANCE_NAME "KitchenConveyor"
#define DEBOUNCE_TIME 1LL
#define MOVE_TIME 1LL

conveyor::conveyor(UA_UInt32 _robot_count) : server_(UA_Server_new()), conveyor_uri_("urn:kitchen:conveyor"), conveyor_type_inserter_(server_, CONVEYOR_TYPE), plate_type_inserter_(server_, PLATE_TYPE),
                                            running_(true), state_status_(conveyor::state::IDLING), work_guard_(boost::asio::make_work_guard(io_context_)), steady_timer_(io_context_),
                                            controller_client_(nullptr), kitchen_client_(nullptr) {
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the conveyor server");
        running_.store(false);
        return;
    }
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC(conveyor_uri_.c_str());
    *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add conveyor attribute nodes */
    conveyor_type_inserter_.add_attribute(CONVEYOR_TYPE, TOTAL_PLATES);
    conveyor_type_inserter_.add_attribute(CONVEYOR_TYPE, OCCUPIED_PLATES);
    /* Add receive finished order notification method node*/
    method_arguments receive_finished_order_notification_arguments;
    receive_finished_order_notification_arguments.add_input_argument("the robot endpoint", "robot_endpoint", UA_TYPES_STRING);
    receive_finished_order_notification_arguments.add_input_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    receive_finished_order_notification_arguments.add_output_argument("the notification received", "notification_received", UA_TYPES_BOOLEAN);
    status = conveyor_type_inserter_.add_method(CONVEYOR_TYPE, FINISHED_ORDER_NOTIFICATION, receive_finished_order_notification, receive_finished_order_notification_arguments, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, FINISHED_ORDER_NOTIFICATION);
        running_.store(false);
        return;
    }
    /* Add receive next robot method node */
    method_arguments receive_next_robot_arguments;
    receive_next_robot_arguments.add_input_argument("the remote robot's position", "robot_position", UA_TYPES_UINT32);
    receive_next_robot_arguments.add_input_argument("the remote robot's endpoint", "robot_endpoint", UA_TYPES_STRING);
    receive_next_robot_arguments.add_input_argument("the recipe id", "recipe_id", UA_TYPES_UINT32);
    receive_next_robot_arguments.add_output_argument("confirms the next robot receival", "result", UA_TYPES_BOOLEAN);
    status = conveyor_type_inserter_.add_method(CONVEYOR_TYPE, RECEIVE_NEXT_ROBOT, receive_next_robot, receive_next_robot_arguments, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, RECEIVE_NEXT_ROBOT);
        return;
    }
    /* Add conveyor type constructor */
    conveyor_type_inserter_.add_object_type_constructor(server_, conveyor_type_inserter_.get_object_type_id(CONVEYOR_TYPE));
    /* Instantiate conveyor type */
    conveyor_type_inserter_.add_object_instance(CONVEYOR_INSTANCE_NAME, CONVEYOR_TYPE);
    UA_UInt32 total_plates_count = _robot_count + 1;
    conveyor_type_inserter_.set_scalar_attribute(CONVEYOR_INSTANCE_NAME, TOTAL_PLATES, &total_plates_count, UA_TYPES_UINT32);
    UA_UInt32 initially_occupied_plates = 0;
    conveyor_type_inserter_.set_scalar_attribute(CONVEYOR_INSTANCE_NAME, OCCUPIED_PLATES, &initially_occupied_plates, UA_TYPES_UINT32);
    /* Setup plates */
    plate::setup_plate_object_type(plate_type_inserter_, server_);
    for (size_t i = 0; i < total_plates_count; i++) {
        plates_.push_back(plate(i,i, conveyor_type_inserter_.get_instance_id(CONVEYOR_INSTANCE_NAME), plate_type_inserter_));
        position_plate_id_map_[i] = i;
    }
    /* Run the conveyor server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error at conveyor startup");
        running_.store(false);
        return;
    }
    /* Register at discovery server repeatedly */
    if (discovery_util_.register_server_repeatedly(server_) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to start discovery register", __FUNCTION__);
        stop();
        return;
    }
    /* Start the conveyor event loop */
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_.load()) {
                UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running conveyor");
        stop();
        return;
    }
    /* Setup controller client */
    std::string controller_endpoint;
    while((status = discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE)) != UA_STATUSCODE_GOOD) {
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        if (!running_.load()) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to controller", __FUNCTION__);
            stop();
            return;
        }
    }
    /* Gather controller method ids */
    if ((method_id_map_[CHOOSE_NEXT_ROBOT] = node_browser_helper().get_method_id(controller_endpoint, CONTROLLER_TYPE, CHOOSE_NEXT_ROBOT)) == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, CHOOSE_NEXT_ROBOT);
        stop();
        return;        
    }
    /* Setup kitchen client */
    std::string kitchen_endpoint;
    while((status = discover_and_connect(kitchen_client_, discovery_util_, kitchen_endpoint, KITCHEN_TYPE)) != UA_STATUSCODE_GOOD) {
        if (!running_.load()) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to kitchen", __FUNCTION__);
            stop();
            return;
        }
    }
    /* Gather kitchen method ids */
    if ((method_id_map_[RECEIVE_COMPLETED_ORDER] = node_browser_helper().get_method_id(kitchen_endpoint, KITCHEN_TYPE, RECEIVE_COMPLETED_ORDER)) == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, RECEIVE_COMPLETED_ORDER);
        stop();
        return;        
    }
}

conveyor::~conveyor() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    stop();
    io_context_.post([this] {
        position_remote_robot_map_.clear();
    });
    join_threads();
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (controller_client_ != nullptr)
            UA_Client_delete(controller_client_);
        if (kitchen_client_ != nullptr)
            UA_Client_delete(kitchen_client_);
    }
    UA_String_clear(&server_endpoint_);
    UA_String_clear(&type_);
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Destructor finished successfully", __FUNCTION__);
}

UA_StatusCode
conveyor::receive_finished_order_notification(UA_Server *_server,
        const UA_NodeId *_session_id, void *_session_context,
        const UA_NodeId *_method_id, void *_method_context,
        const UA_NodeId *_object_id, void *_object_context,
        size_t _input_size, const UA_Variant *_input,
        size_t _output_size, UA_Variant *_output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad input size");
        return UA_STATUSCODE_BAD;
    }

    if (!UA_Variant_hasScalarType(&_input[0], &UA_TYPES[UA_TYPES_STRING])
      ||!UA_Variant_hasScalarType(&_input[1], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input argument type", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }

    UA_String robot_endpoint = *(UA_String*)_input[0].data;
    position_t robot_position = *(position_t*)_input[1].data;
    std::string robot_endpoint_std_str((char*) robot_endpoint.data, robot_endpoint.length);

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    UA_Boolean finished_order_notification_received = true;
    UA_Variant_setScalarCopy(_output, &finished_order_notification_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    conveyor* self = static_cast<conveyor*>(_method_context);
    self->io_context_.post([self, robot_endpoint_std_str, robot_position] {
        self->handle_finished_order_notification(robot_endpoint_std_str, robot_position);
    });
    return UA_STATUSCODE_GOOD;
}

void
conveyor::handle_finished_order_notification(std::string _robot_endpoint, position_t _robot_position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "FINISHED_ORDER_NOTIFICATION: Received notification from robot at position %d with endpoint %s", _robot_position, _robot_endpoint.c_str());
    remove_marked_robots();
    if (position_remote_robot_map_.find(_robot_position) == position_remote_robot_map_.end() || _robot_endpoint.compare(position_remote_robot_map_[_robot_position]->get_endpoint())) {
        position_remote_robot_map_.erase(_robot_position);
        robots_to_be_removed_.erase(_robot_position);
        std::unique_ptr<remote_robot> robot = std::make_unique<remote_robot>(_robot_endpoint, _robot_position,
                                                                            std::bind(&conveyor::mark_robot_for_removal, this, std::placeholders::_1),
                                                                            std::bind(&conveyor::position_swapped_callback, this, std::placeholders::_1, std::placeholders::_2));
        if (robot->initialize_and_start() == UA_STATUSCODE_GOOD) {
            position_remote_robot_map_[_robot_position] = std::move(robot);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot client initialitation/start failed", __FUNCTION__);
            return;
        }
    }
    notifications_map_[_robot_position] = _robot_endpoint;
    if (state_status_ == conveyor::state::IDLING) {
        state_status_ = conveyor::state::MOVING;
        steady_timer_.expires_from_now(std::chrono::milliseconds(DEBOUNCE_TIME * TIME_UNIT));
        steady_timer_.async_wait([this](const boost::system::error_code& _error) {
            if (_error) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling finished orders retrieval", __FUNCTION__);
                stop();
                return;
            }
            handle_retrieve_finished_orders();
        });
    }
}

void
conveyor::handle_retrieve_finished_orders() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    remove_marked_robots();
    for (auto notification = notifications_map_.begin(); notification != notifications_map_.end();) {
        if (!plates_[position_plate_id_map_[notification->first]].is_occupied()) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETRIEVAL: Dish at position %d(%s) is retrievable", notification->first, notification->second.c_str());
            size_t output_size = 0;
            UA_Variant* output = nullptr;
            UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
            if (position_remote_robot_map_.find(notification->first) != position_remote_robot_map_.end()) {
                status = position_remote_robot_map_[notification->first]->handover_finished_order(&output_size, &output);
            }
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETRIEVAL: Retrieving for dish at position %d(%s) failed (%s)", notification->first, notification->second.c_str(), UA_StatusCode_name(status));
                if (output != nullptr)
                    UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                notification = notifications_map_.erase(notification);
                continue;
            }
            handover_finished_order_called(output_size, output);
            notification = notifications_map_.erase(notification);
        } else {
            notification++;
        }
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETRIEVAL: All retrievable dishes passed by robots.");
    request_next_robots();
}

void
conveyor::request_next_robots() {
    for (plate_id_t plate_id : occupied_plates_) {
        plate& p = plates_[plate_id];
        if (!p.is_dish_finished() && p.get_target_position() == 0) {
            request_next_robot(p);
        }
    }
    if (next_robot_request_queue_.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: No next robots requested. ");
        steady_timer_.expires_from_now(std::chrono::milliseconds(MOVE_TIME * TIME_UNIT));
        steady_timer_.async_wait([this](const boost::system::error_code& _error) {
            if (_error) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling conveyor movement", __FUNCTION__);
                stop();
                return;
            }
            move_conveyor(1);
        });
    }
}

void
conveyor::handover_finished_order_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 5) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_STRING])
      || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasScalarType(&_output[2], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasScalarType(&_output[3], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasScalarType(&_output[4], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return;
    }

    UA_String remote_robot_endpoint = *(UA_String*) _output[0].data;
    position_t remote_robot_position = *(position_t*) _output[1].data;
    recipe_id_t finished_recipe = *(recipe_id_t*) _output[2].data;
    UA_UInt32 processed_steps = *(UA_UInt32*) _output[3].data;
    UA_Boolean is_dish_finished = *(UA_Boolean*) _output[4].data;
    std::string remote_robot_endpoint_str = std::string((char*) remote_robot_endpoint.data, remote_robot_endpoint.length);
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
    handle_handover_finished_order(remote_robot_endpoint_str, remote_robot_position, finished_recipe, processed_steps, is_dish_finished);
}

void
conveyor::handle_handover_finished_order(std::string _remote_robot_endpoint, position_t _remote_robot_position, recipe_id_t _finished_recipe, UA_UInt32 _processed_steps, UA_Boolean _is_dish_finished) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_finished_recipe == 0) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "UNCOORDINATED HANDOVER: Robot at position %d passed recipe ID %d with processed steps of %d (%s)", _remote_robot_position, _finished_recipe, _processed_steps, (_is_dish_finished ? "completely" : "partially"));
        return;        
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "HANDOVER: Robot at position %d passed recipe ID %d with processed steps of %d (%s)", _remote_robot_position, _finished_recipe, _processed_steps, (_is_dish_finished ? "completely" : "partially"));
    plate& p = plates_[position_plate_id_map_[_remote_robot_position]];
    p.place_recipe_id(_finished_recipe);
    p.set_occupied(true);
    p.set_dish_finished(_is_dish_finished);
    p.set_processed_steps(_processed_steps);
    occupied_plates_.insert(p.get_plate_id());
    UA_UInt32 occupied_plates_count = occupied_plates_.size();
    conveyor_type_inserter_.set_scalar_attribute(CONVEYOR_INSTANCE_NAME, OCCUPIED_PLATES, &occupied_plates_count, UA_TYPES_UINT32);
}

void
conveyor::request_next_robot(plate& _plate) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Request next robot */
    recipe_id_t finished_recipe = _plate.get_placed_recipe_id();
    UA_UInt32 processed_steps = _plate.get_processed_steps();
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Request next robot for recipe %d with processed steps %d", finished_recipe, processed_steps);
    method_node_caller choose_next_robot_caller;
    choose_next_robot_caller.add_scalar_input_argument(&finished_recipe, UA_TYPES_UINT32);
    choose_next_robot_caller.add_scalar_input_argument(&processed_steps, UA_TYPES_UINT32);
    choose_next_robot_caller.add_scalar_input_argument(&server_endpoint_, UA_TYPES_STRING);
    choose_next_robot_caller.add_scalar_input_argument(&type_, UA_TYPES_STRING);
    object_method_info omi = method_id_map_[CHOOSE_NEXT_ROBOT];
    size_t output_size = 0;
    UA_Variant* output = nullptr;
    UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (controller_client_ != nullptr)
            status = choose_next_robot_caller.call_method_node(controller_client_, omi.object_id_, omi.method_id_, &output_size, &output);
    }
    if (status != UA_STATUSCODE_GOOD) {
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return;
    }
    if(output_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return;
    }
    if(!UA_Variant_hasScalarType(&output[0], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return;
    }
    UA_Boolean result = *(UA_Boolean*) output[0].data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Controller returned %s for next robot request", result ? "true" : "false");
    if (result) {
        next_robot_request_queue_.push(_plate.get_position());
    }
    if (output != nullptr)
        UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
}

UA_StatusCode
conveyor::receive_next_robot(UA_Server* _server,
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
    conveyor* self = static_cast<conveyor*>(_method_context);
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
conveyor::handle_receive_next_robot(position_t _robot_position, std::string _robot_endpoint, recipe_id_t _recipe_id) {
    remove_marked_robots();
    if (next_robot_request_queue_.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: Unexpected response with no outstanding requests (recipe id %d). Ignoring.", _recipe_id);
        return;
    }
    if (_robot_position != 0
        && !_robot_endpoint.empty()
        && (position_remote_robot_map_.find(_robot_position) == position_remote_robot_map_.end()
            || _robot_endpoint.compare(position_remote_robot_map_[_robot_position]->get_endpoint())
    )) {
        position_remote_robot_map_.erase(_robot_position);
        robots_to_be_removed_.erase(_robot_position);
        std::unique_ptr<remote_robot> robot = std::make_unique<remote_robot>(_robot_endpoint, _robot_position,
                                                std::bind(&conveyor::mark_robot_for_removal, this, std::placeholders::_1),
                                                std::bind(&conveyor::position_swapped_callback, this, std::placeholders::_1, std::placeholders::_2));
        if (robot->initialize_and_start() == UA_STATUSCODE_GOOD) {
            position_remote_robot_map_[_robot_position] = std::move(robot);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot client initialitation/start failed", __FUNCTION__);
        }
    }
    // Sanity check
    position_t position_requested_from = next_robot_request_queue_.front();
    plate& p = plates_[position_plate_id_map_[position_requested_from]];
    if (p.get_placed_recipe_id() != _recipe_id) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Mismatch on request mapping", __FUNCTION__);
        return;
    }
    next_robot_request_queue_.pop();
    if (_robot_position != 0 && !_robot_endpoint.empty())
        p.set_target_position(_robot_position);
    else
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: The controller couldn't return a suitable robot for recipe id %d", _recipe_id);

    if (!next_robot_request_queue_.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: %d next robot responses outstanding", next_robot_request_queue_.size());
        return;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT ROBOT: Received all next robot responses");
    steady_timer_.expires_from_now(std::chrono::milliseconds(MOVE_TIME * TIME_UNIT));
    steady_timer_.async_wait([this](const boost::system::error_code& _error) {
        if (_error) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed scheduling conveyor movement", __FUNCTION__);
            stop();
            return;
        }
        move_conveyor(1);
    });
}

void
conveyor::move_conveyor(steps_t _steps) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (size_t i = 0; i < plates_.size(); i++) {
        position_t new_position = (plates_[i].get_position() + _steps) % plates_.size();
        plates_[i].set_position(new_position);
        position_plate_id_map_[new_position] = plates_[i].get_plate_id();
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MOVEMENT: Conveyor moved %d step", _steps);
    deliver_finished_order();
}

void
conveyor::deliver_finished_order() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    remove_marked_robots();
    for (auto occupied_plate_id = occupied_plates_.begin(); occupied_plate_id != occupied_plates_.end();) {
        plate& p = plates_[*occupied_plate_id];
        if (!p.is_dish_finished() && p.get_target_position() == 0) {
            occupied_plate_id++;
            continue;
        }
        /* Deliver finished orders */
        if (p.is_dish_finished() && p.get_position() == OUTPUT_POSITION) {
            method_node_caller receive_completed_order_caller;
            recipe_id_t completed_recipe = p.get_placed_recipe_id();
            receive_completed_order_caller.add_scalar_input_argument(&completed_recipe, UA_TYPES_UINT32);
            object_method_info omi = method_id_map_[RECEIVE_COMPLETED_ORDER];
            size_t output_size = 0;
            UA_Variant* output = nullptr;
            UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
            {
                std::lock_guard<std::mutex> lock(client_mutex_);
                if (kitchen_client_ != nullptr)
                    status = receive_completed_order_caller.call_method_node(kitchen_client_, omi.object_id_, omi.method_id_, &output_size, &output);
            }
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "OUTPUT DELIVERY: Failed to call %s method (%s)", RECEIVE_COMPLETED_ORDER, UA_StatusCode_name(status));
                if (output != nullptr)
                    UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                occupied_plate_id++;
                continue;
            }
            if ((status = receive_completed_order_called(output_size, output)) != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "OUTPUT DELIVERY: Delivery failed because Kitchen returned bad result");
                occupied_plate_id++;
                continue;
            }
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "OUTPUT DELIVERY: Finished dish with recipe id %d delivered at output (%s)", p.get_placed_recipe_id(), UA_StatusCode_name(status));
            reset_plate(p);
            occupied_plate_id = occupied_plates_.erase(occupied_plate_id);
            UA_UInt32 occupied_plates_count = occupied_plates_.size();
            conveyor_type_inserter_.set_scalar_attribute(CONVEYOR_INSTANCE_NAME, OCCUPIED_PLATES, &occupied_plates_count, UA_TYPES_UINT32);
            continue;
        }
        /* Deliver partially prepared orders to next suitable robot */
        if (!p.is_dish_finished() && p.get_position() == p.get_target_position()) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "PREPARE DELIVERY: Dish at position %d is deliverable", p.get_position());
            size_t output_size = 0;
            UA_Variant* output = nullptr;
            if (position_remote_robot_map_.find(p.get_position()) == position_remote_robot_map_.end()) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "PREPARE DELIVERY: Robot at position %d is not known", p.get_position());
                p.set_target_position(0);
                occupied_plate_id++;
                continue;
            }

            remote_robot* target_robot = position_remote_robot_map_[p.get_position()].get();
            if (target_robot->get_position() != p.get_position() || !target_robot->is_available()) {
                p.set_target_position(0);
                occupied_plate_id++;
                continue;
            }
            UA_StatusCode status = target_robot->instruct(p.get_placed_recipe_id(), p.get_processed_steps(), &output_size, &output);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "DELIVERY: Failed to deliver dish at position %d", p.get_position());
                if (output != nullptr)
                    UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                p.set_target_position(0);
                occupied_plate_id++;
                continue;
            }
            if (receive_robot_task_called(output_size, output, p)) {
                reset_plate(p);
                occupied_plate_id = occupied_plates_.erase(occupied_plate_id);
                UA_UInt32 occupied_plates_count = occupied_plates_.size();
                conveyor_type_inserter_.set_scalar_attribute(CONVEYOR_INSTANCE_NAME, OCCUPIED_PLATES, &occupied_plates_count, UA_TYPES_UINT32);
                continue;
            } else {
                p.set_target_position(0);
            }
        }
        occupied_plate_id++;
    }
    determine_next_movement();
}

UA_StatusCode
conveyor::receive_completed_order_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 1) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return UA_STATUSCODE_BAD;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return UA_STATUSCODE_BAD;
    }

    UA_Boolean result = *(UA_Boolean*) _output[0].data;
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
    return result ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BAD;
}

void
conveyor::determine_next_movement() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (!notifications_map_.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT MOVEMENT: There are finished orders to retrieve");
        handle_retrieve_finished_orders();
    } else if (!occupied_plates_.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT MOVEMENT: There are occupied plates with orders to deliver");
        request_next_robots();
    } else {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT MOVEMENT: No occupied plates or orders to retrieve, idling now");   
        state_status_ = conveyor::state::IDLING;
    }
}

bool
conveyor::receive_robot_task_called(size_t _output_size, UA_Variant* _output, plate& _plate) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return false;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return false;
    }

    position_t remote_robot_position = *(position_t*) _output[0].data;
    UA_Boolean result = *(UA_Boolean*) _output[1].data;

    if (!result) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d returned false", __FUNCTION__, remote_robot_position);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return result;
    }

    if (_plate.get_position() != remote_robot_position) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CORRUPTED DELIVERY: Delivery is not valid for plate at position %d for robot at position %d", _plate.get_position(), remote_robot_position);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return false;
    }
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "SUCCESSFUL DELIVERY: Delivered dish at position %d successfully", remote_robot_position);
    return result;
}

void
conveyor::position_swapped_callback(position_t _old_position, position_t _new_position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    io_context_.post([this, _old_position, _new_position] {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "REARRANGING(Conveyor): Reflecting position swap/switch from %d to %d", _old_position, _new_position);
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
        if (position_remote_robot_map_[_old_position] == nullptr) {
            position_remote_robot_map_.erase(_old_position);
            robots_to_be_removed_.erase(_old_position);
        }
        if (position_remote_robot_map_[_new_position] == nullptr) {
            position_remote_robot_map_.erase(_new_position);
            robots_to_be_removed_.erase(_new_position);
        }
    });
}

void
conveyor::mark_robot_for_removal(position_t _position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    io_context_.post([this, _position] {
        robots_to_be_removed_.insert(_position);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Marked robot at position %d for removal", __FUNCTION__, _position);
    });

}

void
conveyor::remove_marked_robots() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (position_t position : robots_to_be_removed_) {
        if (position_remote_robot_map_.find(position) != position_remote_robot_map_.end()) {
            position_remote_robot_map_.erase(position);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Removed remote robot at position %d", __FUNCTION__, position);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: No remote robot found at position %d", __FUNCTION__, position);
        }
    }
    robots_to_be_removed_.clear();
}

void
conveyor::reset_plate(plate& _plate) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    _plate.place_recipe_id(0);
    _plate.set_processed_steps(0);
    _plate.set_target_position(0);
    _plate.set_occupied(false);
    _plate.set_dish_finished(false);
}

void
conveyor::join_threads() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if (client_iterate_thread_.joinable())
        client_iterate_thread_.join();
    if (worker_thread_.joinable())
        worker_thread_.join();
}

void
conveyor::start() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (!running_.load())
        stop();
    /* Lookup own endpoint */
    std::vector<std::string> endpoints;
    while (endpoints.empty()) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Looking up own endpoint", __FUNCTION__);
        if (discovery_util_.lookup_endpoints(endpoints, conveyor_uri_) != UA_STATUSCODE_GOOD || endpoints.empty()) {
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
    type_ = UA_STRING_ALLOC(const_cast<char*>(CONVEYOR_TYPE));
    /* Run the client iterate thread */
    try {
        client_iterate_thread_ = std::thread([this]() {
            while(running_.load()) {
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    /* Handle controller client iterate */
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
                            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Re-established connection to controller", __FUNCTION__);
                            io_context_.post([this] {
                                if (state_status_ == conveyor::state::MOVING && !next_robot_request_queue_.empty()) {
                                    std::queue<position_t>().swap(next_robot_request_queue_);
                                    determine_next_movement();
                                }
                            });
                        }
                    }
                    /* Handle kitchen client iterate */
                    if (kitchen_client_ != nullptr) {
                        UA_StatusCode status = UA_Client_run_iterate(kitchen_client_, 1);
                        if (status != UA_STATUSCODE_GOOD) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running kitchen client iterate", __FUNCTION__);
                            UA_Client_delete(kitchen_client_);
                            kitchen_client_ = nullptr;
                        }
                    } else {
                        std::string kitchen_endpoint;
                        if (discover_and_connect(kitchen_client_, discovery_util_, kitchen_endpoint, KITCHEN_TYPE) == UA_STATUSCODE_GOOD) {
                            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Re-established connection to kitchen", __FUNCTION__);
                        }
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
    /* Setup worker thread */        
    worker_thread_ = std::thread([this]() {
        io_context_.run();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited io_context", __FUNCTION__);
    });
    join_threads();
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited start method", __FUNCTION__);
}

void
conveyor::stop() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    running_.store(false);
    work_guard_.reset();
    io_context_.stop();
    discovery_util_.stop();
    discovery_util_.deregister_server(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Stop finished successfully", __FUNCTION__);
}