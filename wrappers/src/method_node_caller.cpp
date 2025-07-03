#include "../include/method_node_caller.hpp"

#include <open62541/plugin/log_stdout.h>

method_node_caller::method_node_caller() {
}

method_node_caller::~method_node_caller() {
    clear_input_arguments();
}

void
method_node_caller::add_scalar_input_argument(void* _argument_value, UA_UInt32 _type_index, bool _copy) {
    UA_Variant input_argument;
    UA_Variant_init(&input_argument);
    if(_copy) {
        UA_Variant_setScalarCopy(&input_argument, _argument_value, &UA_TYPES[_type_index]);
    } else {
        UA_Variant_setScalar(&input_argument, _argument_value, &UA_TYPES[_type_index]);
    }
    add_input_argument(input_argument, _copy);
}

void
method_node_caller::add_array_input_argument(void* _argument_value, size_t _array_size, UA_UInt32 _type_index, bool _copy) {
    UA_Variant input_argument;
    UA_Variant_init(&input_argument);
    if(_copy) {
        UA_Variant_setArrayCopy(&input_argument, _argument_value, _array_size, &UA_TYPES[_type_index]);
    } else {
        UA_Variant_setArray(&input_argument, _argument_value, _array_size, &UA_TYPES[_type_index]);
    }
    add_input_argument(input_argument, _copy);
}

void
method_node_caller::add_input_argument(UA_Variant& _input_argument, bool _copy) {
    input_arguments_.push_back(_input_argument);
    if(_copy) {
        deep_copy_input_arguments_.push_back(&input_arguments_.back());
    }
}

UA_StatusCode
method_node_caller::call_method_node(UA_Client* _client, UA_NodeId _object_id, UA_NodeId _method_id, UA_ClientAsyncCallCallback _callback, void* _userdata) {
    UA_StatusCode status_code = UA_STATUSCODE_BAD;
    status_code = UA_Client_call_async(_client, _object_id, _method_id, input_arguments_.size(), input_arguments_.data(), _callback, _userdata, NULL);
    return status_code;
}

void
method_node_caller::clear_input_arguments() {
    for (UA_Variant* deep_copy_variant : deep_copy_input_arguments_) {
        UA_Variant_clear(deep_copy_variant);
    }
    input_arguments_.clear();
}