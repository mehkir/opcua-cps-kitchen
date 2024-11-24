#include <open62541/client.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/client_config_default.h>

#include "client_connection_establisher.hpp"
#include "method_node_caller.hpp"
#include "node_value_subscriber.hpp"
#include "method_node_caller.hpp"
#include "node_ids.hpp"

bool running = true;
UA_UInt64 current_clock_tick = 0;
UA_UInt64 next_clock_tick = 0;
node_value_subscriber clock_tick_subscriber;
method_node_caller receive_tick_ack_caller;

struct my_context {
    public:
        void handle_clock_tick_notification(UA_UInt64 new_clock_tick) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "%s called", __FUNCTION__);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "%s: New clock tick is: %lu", __FUNCTION__, new_clock_tick);
            current_clock_tick = new_clock_tick;
            next_clock_tick = new_clock_tick+1;
        }

        void handle_receive_tick_ack_result(UA_Boolean tick_ack_result) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "%s called", __FUNCTION__);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "%s: Tick ack result is: %d", __FUNCTION__, tick_ack_result);
        }
};

static void
receive_tick_ack_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_userdata == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Userdata is NULL");
        return;
    }

    UA_StatusCode status_code = _response->responseHeader.serviceResult;
    if(status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad service result", __FUNCTION__);
        return;
    }

    UA_Boolean tick_ack_result;
    if(UA_Variant_hasScalarType(_response->results[0].outputArguments, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        tick_ack_result = *(UA_Boolean*)_response->results[0].outputArguments->data;
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s result is %d", __FUNCTION__, tick_ack_result);
    } else {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s bad output argument type", __FUNCTION__);
        return;
    }
    
    my_context* tick_clock_context = static_cast<my_context*>(_userdata);
    tick_clock_context->handle_receive_tick_ack_result(tick_ack_result);
}

static void
clock_tick_notification_callback(UA_Client* _client, UA_UInt32 _subscription_id, void* _subscription_context,
                                        UA_UInt32 _monitor_id, void* _monitor_context, UA_DataValue* _value) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT64])) {
        UA_UInt64 new_clock_tick = *(UA_UInt64 *) _value->value.data;
        if (new_clock_tick == 0) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Ignore initial zero notification", __FUNCTION__);
            return;
        }
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New clock tick is: %lu", new_clock_tick);

        my_context* tick_clock_context = static_cast<my_context*>(_monitor_context);
        tick_clock_context->handle_clock_tick_notification(new_clock_tick);
        receive_tick_ack_caller.call_method_node(_client, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, tick_clock_context);
    }
}

int main(int argc, char** argv) {
    UA_UInt16 id = atoi(argv[1]);
    UA_UInt16 clock_port = atoi(argv[2]);
    UA_Client* clock_client = UA_Client_new();
    /* Setup clock client */
    client_connection_establisher clock_client_connection_establisher;
    UA_SessionState clock_session_state = clock_client_connection_establisher.establish_connection(clock_client, clock_port);
    if (clock_session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing clock client session");
        running = false;
        return 0;
    }

    my_context tick_clock_context;
    if (clock_session_state == UA_SESSIONSTATE_ACTIVATED) {
        /* Setup clock tick monitoring */
        UA_StatusCode status = clock_tick_subscriber.subscribe_node_value(clock_client, UA_NODEID_STRING(1, CLOCK_TICK), clock_tick_notification_callback, &tick_clock_context);
        if(status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error subscribing to the clock tick node");
            running = false;
            return 0;
        }

        /* Add receive tick ack method caller to send next tick */
        receive_tick_ack_caller.add_input_argument(&id, UA_TYPES_UINT16);
        receive_tick_ack_caller.add_input_argument(&current_clock_tick, UA_TYPES_UINT64);
        receive_tick_ack_caller.add_input_argument(&next_clock_tick, UA_TYPES_UINT64);
        next_clock_tick = 1;
        receive_tick_ack_caller.call_method_node(clock_client, UA_NODEID_STRING(1, RECEIVE_TICK_ACK), receive_tick_ack_called, &tick_clock_context);

        /* Run the clock client */
        while(running) {
            UA_StatusCode status = UA_Client_run_iterate(clock_client, 100);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running the clock client");
                running = false;
            }
        }
    }
    return 0;
}