#ifndef INFORMATION_NODE_READER_HPP
#define INFORMATION_NODE_READER_HPP

#include <open62541/client_highlevel.h>

class information_node_reader {
private:
    UA_Variant variant_;
public:
    information_node_reader();
    ~information_node_reader();
    UA_StatusCode read_information_node(UA_Client* _client, UA_NodeId _node_id);
    UA_Variant* get_variant();
};

#endif // INFORMATION_NODE_READER_HPP