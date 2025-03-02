#ifndef INFORMATION_NODE_INSERTER_HPP
#define INFORMATION_NODE_INSERTER_HPP

#include <open62541/server.h>
#include <string>

class information_node_inserter {
private:
    /* data */
public:
    information_node_inserter();
    ~information_node_inserter();
    UA_StatusCode add_scalar_node(UA_Server* _server, UA_NodeId _node_id, std::string _browse_name, UA_UInt32 _type_index, void* _value);
    UA_StatusCode add_array_node(UA_Server* _server, UA_NodeId _node_id, std::string _browse_name, UA_UInt32 _type_index, void* _array, size_t _array_size);
};


#endif // INFORMATION_NODE_INSERTER_HPP