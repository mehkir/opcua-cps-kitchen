#ifndef METHOD_NODE_CALLER_HPP
#define METHOD_NODE_CALLER_HPP

#include <vector>
#include <open62541/client_highlevel_async.h>

class method_node_caller {
private:
    std::vector<UA_Variant> input_arguments_;
    std::vector<UA_Variant*> allocated_input_arguments_;
public:
    method_node_caller();
    ~method_node_caller();

    void
    add_input_argument(void* _argument_value, UA_UInt32 _type_index, bool _copy = false);

    UA_StatusCode
    call_method_node(UA_Client* _client, UA_NodeId _method_node_id, UA_ClientAsyncCallCallback _callback, void* _userdata);

    void
    clear_input_arguments();
};


#endif // METHOD_NODE_CALLER_HPP