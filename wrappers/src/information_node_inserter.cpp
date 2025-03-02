#include "../include/information_node_inserter.hpp"

information_node_inserter::information_node_inserter() {
}

information_node_inserter::~information_node_inserter() {
}

UA_StatusCode information_node_inserter::add_variable_node(UA_Server* _server, UA_NodeId _node_id, std::string _browse_name, UA_UInt32 _type_index, void* _value) {
    /* Define the attribute and value of the variable node */
    UA_VariableAttributes variable_attributes = UA_VariableAttributes_default;
    UA_Variant_setScalar(&variable_attributes.value, _value, &UA_TYPES[_type_index]);
    variable_attributes.description = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(_browse_name.c_str()));
    variable_attributes.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(_browse_name.c_str()));
    variable_attributes.dataType = UA_TYPES[_type_index].typeId;
    variable_attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    /* Define where the node shall be added with which browsename */
    UA_NodeId requested_new_node_id = _node_id;
    UA_NodeId parent_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId reference_type_id = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_QualifiedName browse_name = UA_QUALIFIEDNAME(1, const_cast<char*>(_browse_name.c_str()));
    UA_NodeId type_definition = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);

    /* Add the variable node to the information model */
    UA_StatusCode status_code =
    UA_Server_addVariableNode(_server, requested_new_node_id,
        parent_node_id, reference_type_id, browse_name,
        type_definition, variable_attributes, NULL, NULL);
    return status_code;
}