#ifndef INFORMATION_NODE_WRITER_HPP
#define INFORMATION_NODE_WRITER_HPP

#include <open62541/server.h>

class information_node_writer {
    private:
    public:
        information_node_writer();
        ~information_node_writer();
        
        UA_StatusCode
        write_value(UA_Server* _server, UA_NodeId _node_id, void* _value, const UA_DataType* _type);
};

#endif // INFORMATION_NODE_WRITER_HPP