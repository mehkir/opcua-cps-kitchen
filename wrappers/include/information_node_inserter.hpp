/**
 * @file information_node_inserter.hpp
 * @brief Adds scalar or array variable nodes to the server address space.
 */
#ifndef INFORMATION_NODE_INSERTER_HPP
#define INFORMATION_NODE_INSERTER_HPP

#include <open62541/server.h>
#include <string>

/**
 * @brief Convenience wrapper for inserting variable nodes with scalar or array values.
 */
class information_node_inserter {
private:
public:
    /**
     * @brief Constructs a new information node inserter object.
     * 
     */
    information_node_inserter();

    /**
     * @brief Destroys the information node inserter object.
     * 
     */
    ~information_node_inserter();

    /**
     * @brief Adds a scalar node to the address space.
     * 
     * @param _server the server.
     * @param _node_id the node id of the scalar node.
     * @param _browse_name the browse name.
     * @param _type_index the type index.
     * @param _value the value.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    add_scalar_node(UA_Server* _server, UA_NodeId _node_id, std::string _browse_name, UA_UInt32 _type_index, void* _value);

    /**
     * @brief Adds an array node to the address space.
     * 
     * @param _server the server.
     * @param _node_id the node id of the scalar node.
     * @param _browse_name the browse name.
     * @param _type_index the type index.
     * @param _array the array.
     * @param _array_size the array size.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    add_array_node(UA_Server* _server, UA_NodeId _node_id, std::string _browse_name, UA_UInt32 _type_index, void* _array, size_t _array_size);
private:
    /**
     * @brief Shared implementation for scalar/array creation.
     * 
     * @param _server the server.
     * @param _variable_attributes the variable attributes.
     * @param _node_id the node id.
     * @param _browse_name the browse name.
     * @param _type_index the type index.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    add_variable_node(UA_Server* _server, UA_VariableAttributes& _variable_attributes, UA_NodeId _node_id, std::string _browse_name, UA_UInt32 _type_index);
};


#endif // INFORMATION_NODE_INSERTER_HPP