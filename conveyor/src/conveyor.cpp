#include "../include/conveyor.hpp"
#include <open62541/server_config_default.h>
#include "node_ids.hpp"

#include <string>
#include <memory>

conveyor::conveyor(UA_UInt16 _port, UA_UInt32 _robot_count) : server_(UA_Server_new()), port_(_port), running_(true) {
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(server_config, port_, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the conveyor server");
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
    UA_UInt16 robot_port = *(UA_UInt16*)_input[0].data;
    UA_UInt32 robot_position = *(UA_UInt32*)_input[1].data;

    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "method context is NULL");
        return UA_STATUSCODE_BAD;
    }
    conveyor* self = static_cast<conveyor*>(_method_context);
    return UA_STATUSCODE_GOOD;

}

void
conveyor::move_conveyor(UA_UInt32 _steps) {
    for (size_t i = 0; i < plates_.size(); i++) {
        int new_position = (plates_[i].get_position() + _steps) % plates_.size();
        plates_[i].set_position(new_position);
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