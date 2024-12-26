#include "../include/conveyor.hpp"
#include <open62541/server_config_default.h>

#include <string>
#include <memory>
#include "response_checker.hpp"
#include "callback_scheduler.hpp"

#define DEBOUNCE_TIME 1
#define MOVE_TIME 1

conveyor::conveyor(port_t _port, UA_UInt32 _robot_count) : server_(UA_Server_new()), port_(_port), running_(true) {
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the conveyor server");
        running_ = false;
        return;
    }

    /* Setup plates */
    for (size_t i = 0; i < _robot_count+1; i++) {
        plates_.push_back(plate(i,i));
        position_plates_map_[i] = &plates_.back();
    }

    receive_finished_order_notification_inserter_.add_input_argument("robot port", "robot_port", UA_TYPES_UINT16);
    receive_finished_order_notification_inserter_.add_input_argument("robot position", "robot_position", UA_TYPES_UINT16);
    receive_finished_order_notification_inserter_.add_output_argument("notification received", "notification_received", UA_TYPES_BOOLEAN);
    status = receive_finished_order_notification_inserter_.add_method_node(server_, UA_NODEID_STRING(1, FINISHED_ORDER_NOTIFICATION), "receive finished order notification", receive_finished_order_notification, this);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "%s: Error adding the receive finished order notification method node", __FUNCTION__);
        running_ = false;
        return;
    }

    /* Run the conveyor server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error at conveyor startup");
        running_ = false;
        return;
    }
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_StatusCode status = UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error running conveyor");
        running_ = false;
        return;
    }
}

conveyor::~conveyor() {
    running_ = false;
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
    port_t robot_port = *(port_t*)_input[0].data;
    position_t robot_position = *(position_t*)_input[1].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    conveyor* self = static_cast<conveyor*>(_method_context);
    self->handle_finished_order_notification(robot_port, robot_position);
    return UA_STATUSCODE_GOOD;
}

void
conveyor::handle_finished_order_notification(port_t _robot_port, position_t _robot_position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if (!position_remote_robot_map_.count(_robot_position)) {
        position_remote_robot_map_[_robot_position] = std::make_unique<remote_robot>(_robot_port, _robot_position);
    }
    notifications_map_[_robot_position] = _robot_port;
    // TODO: Schedule timer for handovers, after handovers are done, start moving and do handovers again, after move timer is expired
    if (occupied_plates_.empty()) {
        callback_scheduler retrieve_finished_orders_scheduler(server_, retrieve_finished_orders, this, NULL);
        retrieve_finished_orders_scheduler.schedule_from_now(UA_DateTime_nowMonotonic() + ((long long)DEBOUNCE_TIME * UA_DATETIME_SEC));
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
    for (auto notification = notifications_map_.begin(); notification != notifications_map_.end(); notification++) {
        if (!position_plates_map_[notification->first]->is_occupied()){
            retrievable_positions_.insert(notification->first);
        }
    }

    for (position_t position : retrievable_positions_) {
        remote_robot& robot = position_remote_robot_map_[position].operator*();
        robot.handover_finished_order(handover_finished_order_called, this);
    }
}

void
conveyor::handover_finished_order_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Userdata is NULL", __FUNCTION__);
        return;
    }
    response_checker response(_response);
    UA_StatusCode status_code = response.get_service_result();;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad service result", __FUNCTION__);
        return;
    }

    if(!response.get_output_arguments_size(0) != 3) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return;
    }

    if(!response.has_scalar_type(0, 0, &UA_TYPES[UA_TYPES_UINT16])
      ||response.has_scalar_type(0, 1, &UA_TYPES[UA_TYPES_UINT32])
      ||response.has_scalar_type(0, 2, &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }

    port_t remote_robot_port = *(port_t*) response.get_data(0,0);
    position_t remote_robot_position = *(position_t*) response.get_data(0,1);
    recipe_id_t finished_recipe = *(recipe_id_t*) response.get_data(0,2);

    conveyor* self = static_cast<conveyor*>(_userdata);
    self->handle_handover_finished_order(remote_robot_port, remote_robot_position, finished_recipe);
}

void
conveyor::handle_handover_finished_order(port_t _remote_robot_port, position_t _remote_robot_position, recipe_id_t _finished_recipe) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    notifications_map_.erase(_remote_robot_position);
    plate* p = position_plates_map_[_remote_robot_position];
    p->place_recipe_id(_finished_recipe);
    p->set_occupied(true);
    occupied_plates_.insert(p->get_plate_id());
    retrieved_positions_.insert(_remote_robot_position);
    if(retrieved_positions_ == retrievable_positions_) {
        retrieved_positions_.clear();
        retrievable_positions_.clear();
        callback_scheduler movement_scheduler(server_, perform_movement, this, NULL);
        movement_scheduler.schedule_from_now(UA_DateTime_nowMonotonic() + ((long long)DEBOUNCE_TIME * UA_DATETIME_SEC));
    }
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
    // self->determine_next_movement(); TODO: 
}

void
conveyor::move_conveyor(steps_t _steps) {
    for (size_t i = 0; i < plates_.size(); i++) {
        position_t new_position = (plates_[i].get_position() + _steps) % plates_.size();
        plates_[i].set_position(new_position);
        position_plates_map_[new_position] = &plates_[i];
    }
}

void
conveyor::deliver_finished_order() {
    plate* output_plate = position_plates_map_[0];
    if(output_plate->is_occupied()) {
        recipe_id_t recipe = output_plate->get_placed_recipe_id();
        output_plate->place_recipe_id(0);
        output_plate->set_occupied(false);
        occupied_plates_.erase(output_plate->get_plate_id());
    }
}

void
conveyor::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
}

void
conveyor::start() {
    if (!running_)
        return;
    
    join_threads();
}

void
conveyor::stop() {
    running_ = false;
}