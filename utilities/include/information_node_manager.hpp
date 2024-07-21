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
    template<typename T> void add_information_node(UA_Server* _server, std::string _node_id, std::string _browse_name, UA_UInt32 _type_index, T *_value) {
        /* Define the attribute and value of the variable node */
        UA_VariableAttributes variable_attributes = UA_VariableAttributes_default;
        UA_Variant_setScalar(&variable_attributes.value, _value, &UA_TYPES[_type_index]);
        variable_attributes.description = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_browse_name.c_str()));
        variable_attributes.displayName = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_browse_name.c_str()));
        variable_attributes.dataType = UA_TYPES[_type_index].typeId;
        variable_attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

        /* Define where the node shall be added with which browsename */
        UA_NodeId requested_new_node_id = UA_NODEID_STRING(1, const_cast<char*>(_node_id.c_str()));
        UA_NodeId parent_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        UA_NodeId reference_type_id = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
        UA_QualifiedName browse_name = UA_QUALIFIEDNAME(1, const_cast<char*>(_browse_name.c_str()));
        UA_NodeId type_definition = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);

        /* Add the variable node to the information model */
        UA_Server_addVariableNode(_server, requested_new_node_id,
            parent_node_id, reference_type_id, browse_name,
            type_definition, variable_attributes, NULL, NULL);
    }
};


#endif // INFORMATION_NODE_MANAGER_HPP