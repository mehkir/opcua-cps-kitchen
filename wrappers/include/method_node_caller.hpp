#ifndef METHOD_NODE_CALLER_HPP
#define METHOD_NODE_CALLER_HPP

#include <vector>
#include <open62541/client_highlevel_async.h>
#include <open62541/client_highlevel.h>

class method_node_caller {
private:
    std::vector<UA_Variant> input_arguments_;
public:
    method_node_caller();
    ~method_node_caller();

    void
    add_scalar_input_argument(void* _argument_value, UA_UInt32 _type_index);

    void
    add_array_input_argument(void* _argument_value, size_t _array_size, UA_UInt32 _type_index);

    UA_StatusCode
    call_method_node(UA_Client* _client, UA_NodeId _object_id, UA_NodeId _method_id, UA_ClientAsyncCallCallback _callback, void* _userdata);

    UA_StatusCode
    call_method_node(UA_Client* _client, UA_NodeId _object_id, UA_NodeId _method_id, size_t* _output_size, UA_Variant** _output);

private:
    void
    clear_input_arguments();
};


#endif // METHOD_NODE_CALLER_HPP