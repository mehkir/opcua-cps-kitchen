#include "../include/conveyor.hpp"
#include <open62541/server_config_default.h>

#include <string>
#include <memory>
#include "callback_scheduler.hpp"
#include "time_unit.hpp"
#include "filtered_logger.hpp"
#include "discovery_util.hpp"

#define CONVEYOR_INSTANCE_NAME "KitchenConveyor"
#define DEBOUNCE_TIME 1LL
#define MOVE_TIME 1LL

conveyor::conveyor(UA_UInt32 _robot_count) : server_(UA_Server_new()), conveyor_type_inserter_(server_, CONVEYOR_TYPE), plate_type_inserter_(server_, PLATE_TYPE), running_(true), state_status_(conveyor::state::IDLING) {
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error with setting up the conveyor server");
        running_ = false;
        return;
    }
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC("urn:kitchen:conveyor");
    // *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add receive finished order notification method node*/
    method_arguments receive_finished_order_notification_arguments;
    receive_finished_order_notification_arguments.add_input_argument("the robot endpoint", "robot_endpoint", UA_TYPES_STRING);
    receive_finished_order_notification_arguments.add_input_argument("the robot position", "robot_position", UA_TYPES_UINT32);
    receive_finished_order_notification_arguments.add_output_argument("the notification received", "notification_received", UA_TYPES_BOOLEAN);
    status = conveyor_type_inserter_.add_method(CONVEYOR_TYPE, FINISHED_ORDER_NOTIFICATION, receive_finished_order_notification, receive_finished_order_notification_arguments, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, FINISHED_ORDER_NOTIFICATION);
        running_ = false;
        return;
    }
    /* Add conveyor type constructor */
    conveyor_type_inserter_.add_object_type_constructor(server_, conveyor_type_inserter_.get_object_type_id(CONVEYOR_TYPE));
    /* Instantiate conveyor type */
    conveyor_type_inserter_.add_object_instance(CONVEYOR_INSTANCE_NAME, CONVEYOR_TYPE);
    /* Setup plates */
    plate::setup_plate_object_type(plate_type_inserter_, server_);
    for (size_t i = 0; i < _robot_count+1; i++) {
        plates_.push_back(plate(i,i, conveyor_type_inserter_.get_instance_id(CONVEYOR_INSTANCE_NAME), plate_type_inserter_));
        position_plate_id_map_[i] = i;
    }
    /* Run the conveyor server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error at conveyor startup");
        running_ = false;
        return;
    }
    /* Register at discovery and register repeatedly */
    try {
        discovery_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = register_server(server_);
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Registered on discovery server. Renewal in 50 minutes", __FUNCTION__);
                std::unique_lock<std::mutex> lock(discovery_mutex_);
                discovery_cv_.wait_for(lock, std::chrono::minutes(50), [this] { return !running_.load(); });
                if (!running_) {
                    deregister_server(server_);
                    break;
                }
                if (status != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running discovery thread", __FUNCTION__);
                    stop();
                    return;
                }
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error registering on the discovery server");
        stop();
        return;
    }
    /* Start the conveyor event loop */
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running conveyor");
        stop();
        return;
    }
}

conveyor::~conveyor() {
    stop();
    join_threads();
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
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
    UA_String robot_endpoint = *(UA_String*)_input[0].data;
    std::string robot_ep((char*) robot_endpoint.data, robot_endpoint.length);
    position_t robot_position = *(position_t*)_input[1].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    conveyor* self = static_cast<conveyor*>(_method_context);
    self->handle_finished_order_notification(robot_ep, robot_position, _output);
    return UA_STATUSCODE_GOOD;
}

void
conveyor::handle_finished_order_notification(std::string _robot_endpoint, position_t _robot_position, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "FINISHED_ORDER_NOTIFICATION: Received notification from robot at position %d with endpoint %s", _robot_position, _robot_endpoint.c_str());
    UA_Boolean finished_order_notification_received = true;
    UA_Variant_setScalarCopy(_output, &finished_order_notification_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
    if (position_remote_robot_map_.find(_robot_position) == position_remote_robot_map_.end()) {
        position_remote_robot_map_[_robot_position] = std::make_unique<remote_robot>(_robot_endpoint, _robot_position);
    }
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
    for (auto notification = notifications_map_.begin(); notification != notifications_map_.end();) {
        if (!plates_[position_plate_id_map_[notification->first]].is_occupied()){
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "RETRIEVAL: Dish at position %d is retrievable", notification->first);
            size_t output_size;
            UA_Variant* output;
            UA_StatusCode status = position_remote_robot_map_[notification->first]->handover_finished_order(&output_size, &output);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: RETRIEVAL: Retrieving for dish at position %d failed", __FUNCTION__, notification->first);
                stop();
                return;
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
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 6) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        stop();
        return;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_STRING])
      || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasScalarType(&_output[2], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasScalarType(&_output[3], &UA_TYPES[UA_TYPES_UINT32])
      || !UA_Variant_hasScalarType(&_output[4], &UA_TYPES[UA_TYPES_STRING])
      || !UA_Variant_hasScalarType(&_output[5], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        stop();
        return;
    }

    UA_String remote_robot_endpoint = *(UA_String*) _output[0].data;
    position_t remote_robot_position = *(position_t*) _output[1].data;
    recipe_id_t finished_recipe = *(recipe_id_t*) _output[2].data;
    UA_UInt32 processed_steps = *(UA_UInt32*) _output[3].data;
    UA_String next_remote_robot_endpoint = *(UA_String*) _output[4].data;
    position_t next_remote_robot_position = *(position_t*) _output[5].data;

    handle_handover_finished_order(std::string((char*) remote_robot_endpoint.data, remote_robot_endpoint.length), remote_robot_position, finished_recipe, processed_steps, std::string((char*) next_remote_robot_endpoint.data, next_remote_robot_endpoint.length), next_remote_robot_position);
}

void
conveyor::handle_handover_finished_order(std::string _remote_robot_endpoint, position_t _remote_robot_position, recipe_id_t _finished_recipe, UA_UInt32 _processed_steps, std::string _next_remote_robot_endpoint, position_t _next_remote_robot_position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (!_next_remote_robot_endpoint.empty() && _next_remote_robot_position != 0) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "HANDOVER: Robot at position %d passed recipe ID %d with processed steps of %d for next robot at position %d", _remote_robot_position, _finished_recipe, _processed_steps, _next_remote_robot_position);
    } else {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "HANDOVER: Robot at position %d passed recipe ID %d with processed steps of %d", _remote_robot_position, _finished_recipe, _processed_steps);
    }
    if (!_next_remote_robot_endpoint.empty() && _next_remote_robot_position != 0 && position_remote_robot_map_.find(_next_remote_robot_position) == position_remote_robot_map_.end()) {
        position_remote_robot_map_[_next_remote_robot_position] = std::make_unique<remote_robot>(_next_remote_robot_endpoint, _next_remote_robot_position);
    }
    plate& p = plates_[position_plate_id_map_[_remote_robot_position]];
    p.place_recipe_id(_finished_recipe);
    p.set_occupied(true);
    p.set_processed_steps(_processed_steps);
    if (!_next_remote_robot_endpoint.empty() && _next_remote_robot_position != 0)
        p.set_target_robot(position_remote_robot_map_[_next_remote_robot_position].get());
    occupied_plates_.insert(p.get_plate_id());
}


void
conveyor::perform_movement(UA_Server* _server, void* _data) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
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
    for (size_t i = 0; i < plates_.size(); i++) {
        position_t new_position = (plates_[i].get_position() + _steps) % plates_.size();
        plates_[i].set_position(new_position);
        position_plate_id_map_[new_position] = plates_[i].get_plate_id();
    }
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "MOVEMENT: Conveyor moved %d step", _steps);
}

void
conveyor::deliver_finished_order() {
    for (size_t i = 0; i < plates_.size(); i++) {
        plate& p = plates_[i];
        if (!p.is_occupied()) {
            continue;
        }
        if (p.get_target_robot() != nullptr && (p.get_target_robot()->get_position() == p.get_position())) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "PREPARE DELIVERY: Dish at position %d is deliverable", p.get_position());
            size_t output_size;
            UA_Variant* output;
            UA_StatusCode status = p.get_target_robot()->instruct(p.get_placed_recipe_id(), p.get_processed_steps(), &output_size, &output);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "DELIVERY: Failed to deliver dish at position %d", p.get_position());
                continue;
            }
            receive_robot_task_called(output_size, output);
        }
        if (p.get_position() == OUTPUT_POSITION) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "OUTPUT DELIVERY: Finished dish with recipe id %d delivered at output", p.get_placed_recipe_id());
            p.place_recipe_id(0);
            p.set_occupied(false);
            p.set_processed_steps(0);
            occupied_plates_.erase(p.get_plate_id());
        }
    }
    determine_next_movement();
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

void
conveyor::receive_robot_task_called(size_t _output_size, UA_Variant* _output) {
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

    position_t remote_robot_position = *(position_t*) _output[1].data;
    UA_Boolean result = *(position_t*) _output[2].data;

    if (!result) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d returned false", __FUNCTION__, remote_robot_position);
        stop();
        return;
    }

    plate& p = plates_[position_plate_id_map_[remote_robot_position]];
    // Sanity check
    if (!p.is_occupied() || p.get_target_robot() == nullptr || p.get_target_robot()->get_position() != remote_robot_position) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "CORRUPTED DELIVERY: Delivery is not valid for plate at position %d for robot at position %d", p.get_position(), remote_robot_position);
        stop();
        return;    
    }
    p.place_recipe_id(0);
    p.set_occupied(false);
    p.set_processed_steps(0);
    p.set_target_robot(nullptr);
    occupied_plates_.erase(p.get_plate_id());
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "SUCCESSFUL DELIVERY: Delivered dish at position %d successfully", remote_robot_position);
}

void
conveyor::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if (discovery_thread_.joinable())
        discovery_thread_.join();
}

void
conveyor::start() {
    if (!running_)
        stop();
    join_threads();
}

void
conveyor::stop() {
    running_ = false;
    discovery_cv_.notify_all();
}