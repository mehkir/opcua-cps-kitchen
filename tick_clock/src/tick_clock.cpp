#include "../include/tick_clock.hpp"
#include "node_ids.hpp"
#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>

tick_clock::tick_clock(UA_UInt16 _clock_port, UA_UInt32 _clock_client_count) : clock_server_(UA_Server_new()), clock_port_(_clock_port), clock_tick_(0), next_clock_tick_(0), clock_client_count_(_clock_client_count), running_(true) {
    UA_ServerConfig* clock_server_config = UA_Server_getConfig(clock_server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(clock_server_config, _clock_port, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error with setting up the server");
        UA_Server_delete(clock_server_);
        return;
    }

    status = clock_tick_inserter_.add_information_node(clock_server_, UA_NODEID_STRING(1, CLOCK_TICK), "the clock tick", UA_TYPES_UINT64, &clock_tick_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error adding clock_tick information node");
        return;
    }

    receive_tick_ack_inserter_.add_input_argument("port of the tick client", "port", UA_TYPES_UINT16);
    receive_tick_ack_inserter_.add_input_argument("current tick of the client", "current_client_tick", UA_TYPES_UINT64);
    receive_tick_ack_inserter_.add_input_argument("next tick of the tick client", "next_tick", UA_TYPES_UINT64);
    receive_tick_ack_inserter_.add_output_argument("ack received", "ack_received", UA_TYPES_BOOLEAN);
    status = receive_tick_ack_inserter_.add_method_node(clock_server_, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), "receive tick ack", receive_tick_ack, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error adding method node");
    }
}

tick_clock::~tick_clock() {
    running_ = false;
    UA_Server_delete(clock_server_);
}

UA_StatusCode tick_clock::receive_tick_ack(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    /* Extract input arguments */
    UA_UInt16 port = *(UA_UInt16*)_input[0].data;
    UA_UInt64 current_client_tick = *(UA_UInt64*)_input[1].data;
    UA_UInt64 next_tick = *(UA_UInt64*)_input[2].data;
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Extracted inputs: port: %d, current_client_tick: %lu, next_tick: %lu", port, current_client_tick, next_tick);
    tick_clock* self = static_cast<tick_clock*>(_method_context);
    self->handle_receive_tick_ack(current_client_tick, next_tick, port, _output);
    return UA_STATUSCODE_GOOD;
}

void tick_clock::handle_receive_tick_ack(UA_UInt64 _current_client_tick, UA_UInt64 _next_tick, UA_UInt16 _port, UA_Variant* _output) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
    /* Check if the current tick of the client is equal to the current tick of the server */
    if (_current_client_tick != clock_tick_) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "current tick of the client is not equal to the current tick of the server");
        UA_Boolean ack_received = false;
        UA_Variant_setScalarCopy(_output, &ack_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
        return;
    }
    /* Determine the next smallest tick and if all clients sent their next ticks */
    if(currently_acknowledged_set_.size() != clock_client_count_) {
        if (_next_tick > clock_tick_) {
            if (next_clock_tick_ == clock_tick_) {
                next_clock_tick_ = _next_tick;
            } else {
                next_clock_tick_ = _next_tick < next_clock_tick_ ? _next_tick : next_clock_tick_;
            }
        }
        currently_acknowledged_set_.insert(_port);
        UA_Boolean ack_received = true;
        UA_Variant_setScalarCopy(_output, &ack_received, &UA_TYPES[UA_TYPES_BOOLEAN]);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "TICKS: Next tick received(port=%d, current_tick=%d, next_tick=%d", _port, _current_client_tick, _next_tick);
    }
    /* Update/fast-forward clock to determined next tick */
    if (currently_acknowledged_set_.size() == clock_client_count_) {
        UA_Variant new_clock_tick;
        UA_Variant_setScalar(&new_clock_tick, &next_clock_tick_, &UA_TYPES[UA_TYPES_UINT64]);
        currently_acknowledged_set_.clear();
        clock_tick_ = next_clock_tick_;
        UA_Server_writeValue(clock_server_, UA_NODEID_STRING(1, CLOCK_TICK), new_clock_tick);
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "TICKS: New clock tick is: %lu", clock_tick_);
    }
}

void tick_clock::start() {
    UA_StatusCode status = UA_Server_run(clock_server_, &running_);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Error starting the server");
        running_ = false;
    }
}

void tick_clock::stop() {
    running_ = false;
}