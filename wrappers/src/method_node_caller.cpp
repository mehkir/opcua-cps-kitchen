#include "../include/method_node_caller.hpp"

#include <open62541/plugin/log_stdout.h>

method_node_caller::method_node_caller() {
}

method_node_caller::~method_node_caller() {
    clear_input_arguments();
}

void
method_node_caller::add_input_argument(void* _argument_value, UA_UInt32 _type_index, bool _copy) {
    UA_Variant input_argument;
    UA_Variant_init(&input_argument);
    if(_copy) {
        UA_Variant_setScalarCopy(&input_argument, _argument_value, &UA_TYPES[_type_index]);
    } else {
        UA_Variant_setScalar(&input_argument, _argument_value, &UA_TYPES[_type_index]);
    }
    input_arguments_.push_back(input_argument);
    if(_copy) {
        allocated_input_arguments_.push_back(&input_arguments_.back());
    }
}

UA_StatusCode
method_node_caller::call_method_node(UA_Client* _client, UA_NodeId _method_node_id, UA_ClientAsyncCallCallback _callback, void* _userdata) {
    UA_StatusCode status_code = UA_STATUSCODE_BAD;
    status_code = UA_Client_call_async(_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), _method_node_id, input_arguments_.size(), input_arguments_.data(), _callback, _userdata, NULL);
    return status_code;
}

void
method_node_caller::clear_input_arguments() {
    for(UA_Variant* allocated_input_argument : allocated_input_arguments_) {
        UA_Variant_clear(allocated_input_argument);
    }
    input_arguments_.clear();
    allocated_input_arguments_.clear();
}