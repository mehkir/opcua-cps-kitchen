#ifndef INFORMATION_NODE_MANAGER_HPP
#define INFORMATION_NODE_MANAGER_HPP

#include <open62541/server.h>
#include <string>

class information_node_manager
{
private:
    /* data */
public:
    information_node_manager(UA_Server* _server);
    ~information_node_manager();
    void add_information_node(UA_Server* _server, std::string _node_id, std::string _browse_name, UA_UInt32 _type_index, void *_value);
};


#endif // INFORMATION_NODE_MANAGER_HPP