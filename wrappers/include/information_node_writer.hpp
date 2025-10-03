/**
 * @file information_node_writer.hpp
 * @brief Write scalar values into existing variable nodes in the address space.
 */
#ifndef INFORMATION_NODE_WRITER_HPP
#define INFORMATION_NODE_WRITER_HPP

#include <open62541/server.h>

class information_node_writer {
    private:
    public:
        /**
         * @brief Constructs a new information node writer object.
         * 
         */
        information_node_writer();

        /**
         * @brief Destroys the information node writer object.
         * 
         */
        ~information_node_writer();
        /**
         * @brief Write a value into a variable node.
         * @param _server the server.
         * @param _node_id the target node id.
         * @param _value pointer to value memory.
         * @param _type data type descriptor.
         * @return UA_StatusCode the status code.
         */
        UA_StatusCode write_value(UA_Server* _server, UA_NodeId _node_id, void* _value, const UA_DataType* _type);
};

#endif // INFORMATION_NODE_WRITER_HPP