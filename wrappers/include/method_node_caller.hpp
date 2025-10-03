/**
 * @file method_node_caller.hpp
 * @brief Utilities to construct input argument lists and call OPC UA method nodes (sync & async).
 */
#ifndef METHOD_NODE_CALLER_HPP
#define METHOD_NODE_CALLER_HPP

#include <vector>
#include <open62541/client_highlevel_async.h>
#include <open62541/client_highlevel.h>

/**
 * @brief Prepares and invokes method node calls with scalar or array input arguments.
 */
class method_node_caller {
private:
    std::vector<UA_Variant> input_arguments_; /**< Prepared input argument variants. */
public:
    /**
     * @brief Constructs a new method node caller object.
     * 
     */
    method_node_caller();

    /**
     * @brief Destroys the method node caller object.
     * 
     */
    ~method_node_caller();

    /**
     * @brief Adds a scalar input argument.
     * 
     * @param _argument_value the pointer to value memory.
     * @param _type_index the type descriptor of the value.
     */
    void
    add_scalar_input_argument(void* _argument_value, UA_UInt32 _type_index);

    /**
     * @brief Adds a array input argument
     * 
     * @param _argument_value the pointer to array memory.
     * @param _array_size the array size.
     * @param _type_index the type descriptor of the array.
     */
    void
    add_array_input_argument(void* _argument_value, size_t _array_size, UA_UInt32 _type_index);

    /**
     * @brief Calls a method on another OPC UA host asynchronously.
     * 
     * @param _client the client.
     * @param _object_id the parent object id.
     * @param _method_id the method id.
     * @param _callback the callback when the method returns.
     * @param _userdata user-defined data passed to the callback.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    call_method_node(UA_Client* _client, UA_NodeId _object_id, UA_NodeId _method_id, UA_ClientAsyncCallCallback _callback, void* _userdata);

    /**
     * @brief Calls a method on another OPC UA host synchronously.
     * 
     * @param _client the client.
     * @param _object_id the parent object id.
     * @param _method_id the method id.
     * @param _output_size the output size.
     * @param _output the output pointer containing the returned values.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    call_method_node(UA_Client* _client, UA_NodeId _object_id, UA_NodeId _method_id, size_t* _output_size, UA_Variant** _output);

private:
    void
    clear_input_arguments();
};


#endif // METHOD_NODE_CALLER_HPP