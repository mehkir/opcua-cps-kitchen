/**
 * @file method_node_inserter.hpp
 * @brief Helpers for defining method node metadata and inserting method nodes into a server.
 */
#ifndef METHOD_NODE_INSERTER_HPP
#define METHOD_NODE_INSERTER_HPP

#include <vector>
#include <string>
#include <open62541/server.h>

/**
 * @brief Manages arguments and insertion of an OPC UA method node.
 */
class method_node_inserter {
private:
    std::vector<UA_Argument> input_arguments_; /**< prepared input argument definitions. */
    std::vector<UA_Argument> output_arguments_; /**< prepared output argument definitions. */
    bool is_method_node_added_; /**< guard against multiple insertions. */
    UA_MethodAttributes method_attributes_; /**< attributes applied to method node. */
public:
    /**
     * @brief Constructs a new method node inserter object.
     * 
     */
    method_node_inserter();

    /**
     * @brief Destroys the method node inserter object.
     * 
     */
    ~method_node_inserter();

    /**
     * @brief Adds or appends input arguments to the method.
     * 
     * @param _description the method description.
     * @param _name the method name.
     * @param _type_index the argument's type descriptor.
     */
    void
    add_input_argument(std::string _description, std::string _name, UA_UInt32 _type_index);

    /**
     * @brief Adds or appends output arguments to the method.
     * 
     * @param _description the method description.
     * @param _name the method name.
     * @param _type_index the argument's type descriptor.
     */
    void
    add_output_argument(std::string _description, std::string _name, UA_UInt32 _type_index);

    /**
     * @brief 
     * 
     * @param _server the server.
     * @param _method_node_id the method id.
     * @param _browse_name the browse name in the address space.
     * @param _method_callback the callback when the method is called.
     * @param _node_context user-defined data passed to the callback.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    add_method_node(UA_Server* _server, UA_NodeId _method_node_id, std::string _browse_name, UA_MethodCallback _method_callback, void* _node_context = NULL);
};

#endif // METHOD_NODE_INSERTER_HPP