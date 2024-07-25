#ifndef METHOD_NODE_INSERTER_HPP
#define METHOD_NODE_INSERTER_HPP

#include <vector>
#include <string>
#include <open62541/server.h>

class method_node_inserter {
private:
    std::vector<UA_Argument> input_arguments_;
    std::vector<UA_Argument> output_arguments_;
    bool is_method_node_added_;
    UA_MethodAttributes method_attributes_;
public:
    method_node_inserter();
    ~method_node_inserter();
    void add_input_argument(std::string _description, std::string _name, UA_UInt32 _type_index);
    void add_output_argument(std::string _description, std::string _name, UA_UInt32 _type_index);
    UA_StatusCode add_method_node(UA_Server* _server, UA_UInt32 _method_node_id, std::string _browse_name, UA_MethodCallback _method_callback);
};

#endif // METHOD_NODE_INSERTER_HPP