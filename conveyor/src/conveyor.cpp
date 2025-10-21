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

conveyor::conveyor(UA_UInt32 _robot_count) : server_(UA_Server_new()), conveyor_type_inserter_(server_, CONVEYOR_TYPE), plate_type_inserter_(server_, PLATE_TYPE), running_(true), state_status_(conveyor::state::IDLING), controller_client_(nullptr), kitchen_client_(nullptr) {
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the conveyor server");
        running_.store(false);
        return;
    }
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC("urn:kitchen:conveyor");
    // *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    stop();
    join_threads();
    {
        std::lock_guard<std::mutex> lock(position_remote_robot_map_mutex_);
        position_remote_robot_map_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (controller_client_ != nullptr)
            UA_Client_delete(controller_client_);
        if (kitchen_client_ != nullptr)
            UA_Client_delete(kitchen_client_);
    }
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_input_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Bad input size");
        return UA_STATUSCODE_BAD;
    }
    UA_String robot_endpoint = *(UA_String*)_input[0].data;
    position_t robot_position = *(position_t*)_input[1].data;
    std::string robot_endpoint_std_str((char*) robot_endpoint.data, robot_endpoint.length);

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    conveyor* self = static_cast<conveyor*>(_method_context);
    self->handle_finished_order_notification(robot_endpoint_std_str, robot_position, _output);
    return UA_STATUSCODE_GOOD;
}

void
conveyor::handle_finished_order_notification(std::string _robot_endpoint, position_t _robot_position, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "FINISHED_ORDER_NOTIFICATION: Received notification from robot at position %d with endpoint %s", _robot_position, _robot_endpoint.c_str());
    remove_marked_robots();
    {
        std::lock_guard<std::mutex> lock(position_remote_robot_map_mutex_);
        if (position_remote_robot_map_.find(_robot_position) == position_remote_robot_map_.end()) {
            position_remote_robot_map_[_robot_position] = std::make_unique<remote_robot>(_robot_endpoint, _robot_position, std::bind(&conveyor::mark_robot_for_removal, this, std::placeholders::_1),
                                                                                        std::bind(&conveyor::position_swapped_callback, this, std::placeholders::_1, std::placeholders::_2));
        }
    }
    UA_Boolean finished_order_notification_received = true;
    bool remote_robot_initialization_failed = false;
    {
        std::lock_guard<std::mutex> lock(mark_for_removal_mutex_);
        remote_robot_initialization_failed = robots_to_be_removed_.find(_robot_position) != robots_to_be_removed_.end();
    }
    if (remote_robot_initialization_failed) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot initialization at position %d failed", __FUNCTION__, _robot_position);
        remove_marked_robots();
        finished_order_notification_received = false;
        UA_Variant_setScalarCopy(_output, &finished_order_notification_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
        return;
    }
    UA_Variant_setScalarCopy(_output, &finished_order_notification_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    notifications_map_[_robot_position] = _robot_endpoint;
    if (state_status_ == conveyor::state::IDLING) {
        state_status_ = conveyor::state::MOVING;
        callback_scheduler retrieve_finished_orders_scheduler(server_, retrieve_finished_orders, this, NULL);
        retrieve_finished_orders_scheduler.schedule_from_now_relative(DEBOUNCE_TIME * TIME_UNIT);
    }
}

void
conveyor::retrieve_finished_orders(UA_Server* _server, void* _data) {
    if (_data == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Data is NULL", __FUNCTION__);
        return;
    }
    conveyor* self = static_cast<conveyor*>(_data);
    self->handle_retrieve_finished_orders();
}

void
conveyor::handle_retrieve_finished_orders() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    remove_marked_robots();
    for (auto notification = notifications_map_.begin(); notification != notifications_map_.end();) {
        if (!plates_[position_plate_id_map_[notification->first]].is_occupied()) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETRIEVAL: Dish at position %d is retrievable", notification->first);
            size_t output_size = 0;
            UA_Variant* output = nullptr;
            UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
            {
                std::lock_guard<std::mutex> lock(position_remote_robot_map_mutex_);
                status = position_remote_robot_map_[notification->first]->handover_finished_order(&output_size, &output);
            }
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: RETRIEVAL: Retrieving for dish at position %d failed (%s)", __FUNCTION__, notification->first, UA_StatusCode_name(status));
                if (output != nullptr)
                    UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                remove_marked_robots();
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
    callback_scheduler movement_scheduler(server_, perform_movement, this, NULL);
    movement_scheduler.schedule_from_now_relative(MOVE_TIME * TIME_UNIT);
}

void
conveyor::handover_finished_order_called(size_t _output_size, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    remove_marked_robots();
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
    if (_is_dish_finished)
        return;
    request_next_robot(p);
}

void
conveyor::request_next_robot(plate& _plate) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Request next robot */
    recipe_id_t finished_recipe = _plate.get_placed_recipe_id();
    UA_UInt32 processed_steps = _plate.get_processed_steps();
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Request next robot for recipe %d with processed steps %d", finished_recipe, processed_steps);
    method_node_caller choose_next_robot_caller;
    choose_next_robot_caller.add_scalar_input_argument(&finished_recipe, UA_TYPES_UINT32);
    choose_next_robot_caller.add_scalar_input_argument(&processed_steps, UA_TYPES_UINT32);
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
    if(output_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return;
    }
    if(!UA_Variant_hasScalarType(&output[0], &UA_TYPES[UA_TYPES_STRING])
        || !UA_Variant_hasScalarType(&output[1], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return;
    }
    UA_String target_endpoint = *(UA_String*) output[0].data;
    position_t target_position = *(position_t*) output[1].data;
    std::string target_endpoint_std_str((char*) target_endpoint.data, target_endpoint.length);
    if (target_endpoint_std_str.empty() || target_position == 0) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: No suitable robot for next steps received", __FUNCTION__);
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        return;
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CHOOSE NEXT ROBOT: Controller returned robot at position %d with endpoint %s", target_position, target_endpoint_std_str.c_str());
    {
        std::lock_guard<std::mutex> lock(position_remote_robot_map_mutex_);
        if (position_remote_robot_map_.find(target_position) == position_remote_robot_map_.end()) {
            position_remote_robot_map_[target_position] = std::make_unique<remote_robot>(target_endpoint_std_str, target_position, std::bind(&conveyor::mark_robot_for_removal, this, std::placeholders::_1),
                                                                                        std::bind(&conveyor::position_swapped_callback, this, std::placeholders::_1, std::placeholders::_2));
        }
    }
    bool remote_robot_initialization_failed = false;
    {
        std::lock_guard<std::mutex> lock(mark_for_removal_mutex_);
        remote_robot_initialization_failed = robots_to_be_removed_.find(target_position) != robots_to_be_removed_.end();
    }
    if (remote_robot_initialization_failed) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot initialization at position %d failed", __FUNCTION__, target_position);
        if (output != nullptr)
            UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        remove_marked_robots();
        return;
    }
    _plate.set_target_position(target_position);
    if (output != nullptr)
        UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
}

void
conveyor::perform_movement(UA_Server* _server, void* _data) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (_data == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Data is NULL", __FUNCTION__);
        return;
    }
    conveyor* self = static_cast<conveyor*>(_data);
    self->move_conveyor(1);
    self->deliver_finished_order();
}

void
conveyor::move_conveyor(steps_t _steps) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (size_t i = 0; i < plates_.size(); i++) {
        position_t new_position = (plates_[i].get_position() + _steps) % plates_.size();
        plates_[i].set_position(new_position);
        position_plate_id_map_[new_position] = plates_[i].get_plate_id();
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MOVEMENT: Conveyor moved %d step", _steps);
}

void
conveyor::deliver_finished_order() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    remove_marked_robots();
    for (auto occupied_plate_id = occupied_plates_.begin(); occupied_plate_id != occupied_plates_.end();) {
        plate& p = plates_[*occupied_plate_id];
        if (!p.is_dish_finished() && p.get_target_position() == 0) {
            request_next_robot(p);
        }
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
            std::lock_guard<std::mutex> lock(position_remote_robot_map_mutex_);
            if (position_remote_robot_map_.find(p.get_position()) == position_remote_robot_map_.end()) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "PREPARE DELIVERY: Robot at position %d is not known", p.get_position());
                request_next_robot(p);
                occupied_plate_id++;
                continue;
            }
            remote_robot* target_robot = position_remote_robot_map_[p.get_position()].get();
            UA_StatusCode status = target_robot->instruct(p.get_placed_recipe_id(), p.get_processed_steps(), &output_size, &output);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "DELIVERY: Failed to deliver dish at position %d", p.get_position());
                if (output != nullptr)
                    UA_Array_delete(output, output_size, &UA_TYPES[UA_TYPES_VARIANT]);
                occupied_plate_id++;
                continue;
            }
            if (receive_robot_task_called(output_size, output)) {
                occupied_plate_id = occupied_plates_.erase(occupied_plate_id);
                UA_UInt32 occupied_plates_count = occupied_plates_.size();
                conveyor_type_inserter_.set_scalar_attribute(CONVEYOR_INSTANCE_NAME, OCCUPIED_PLATES, &occupied_plates_count, UA_TYPES_UINT32);
                continue;
            }
        }
        occupied_plate_id++;
    }
    determine_next_movement();
}

UA_StatusCode
conveyor::receive_completed_order_called(size_t _output_size, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
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
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (occupied_plates_.empty() && notifications_map_.empty()) {
        state_status_ = conveyor::state::IDLING;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT MOVEMENT: No occupied plates or notifications, idling now");
    } else {
        handle_retrieve_finished_orders();
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "NEXT MOVEMENT: There are still finished orders to deliver or retrieve");
    }
}

bool
conveyor::receive_robot_task_called(size_t _output_size, UA_Variant* _output) {
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

    plate& p = plates_[position_plate_id_map_[remote_robot_position]];
    // Sanity check
    if (!p.is_occupied() || p.get_target_position() == 0 || p.get_position() != remote_robot_position) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CORRUPTED DELIVERY: Delivery is not valid for plate at position %d for robot at position %d", p.get_position(), remote_robot_position);
        if (_output != nullptr)
            UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
        stop();
        return false;
    }
    reset_plate(p);
    if (_output != nullptr)
        UA_Array_delete(_output, _output_size, &UA_TYPES[UA_TYPES_VARIANT]);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "SUCCESSFUL DELIVERY: Delivered dish at position %d successfully", remote_robot_position);
    return result;
}

void
conveyor::position_swapped_callback(position_t _old_position, position_t _new_position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::lock_guard<std::mutex> lock(position_remote_robot_map_mutex_);
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
    if (position_remote_robot_map_[_old_position] == nullptr)
        position_remote_robot_map_.erase(_old_position);
    if (position_remote_robot_map_[_new_position] == nullptr)
        position_remote_robot_map_.erase(_new_position);
}

void
conveyor::mark_robot_for_removal(position_t _position) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::lock_guard<std::mutex> lock(mark_for_removal_mutex_);
    robots_to_be_removed_.insert(_position);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Marked robot at position %d for removal", __FUNCTION__, _position);
}

void
conveyor::remove_marked_robots() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::unordered_set<cps_kitchen::plate_id_t> robots_to_be_removed_tmp;
    {
        std::lock_guard<std::mutex> lock(mark_for_removal_mutex_);
        robots_to_be_removed_tmp.swap(robots_to_be_removed_);
    }
    std::lock_guard<std::mutex> lock(position_remote_robot_map_mutex_);
    for (position_t position : robots_to_be_removed_tmp) {
        if (position_remote_robot_map_.find(position) != position_remote_robot_map_.end()) {
            position_remote_robot_map_.erase(position);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Removed remote robot at position %d", __FUNCTION__, position);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: No remote robot found at position %d", __FUNCTION__, position);
        }
    }
}

void
conveyor::reset_plate(plate& _plate) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    _plate.place_recipe_id(0);
    _plate.set_processed_steps(0);
    _plate.set_target_position(0);
    _plate.set_occupied(false);
    _plate.set_dish_finished(false);
}

void
conveyor::join_threads() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if (client_iterate_thread_.joinable())
        client_iterate_thread_.join();
}

void
conveyor::start() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (!running_.load())
        stop();
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
    join_threads();
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited start method", __FUNCTION__);
}

void
conveyor::stop() {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    running_.store(false);
    discovery_util_.stop();
    discovery_util_.deregister_server(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Stop finished successfully", __FUNCTION__);
}