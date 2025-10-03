/**
 * @file information_node_reader.hpp
 * @brief Reads variable values from server or client side (server internal read or client service call).
 */
#ifndef INFORMATION_NODE_READER_HPP
#define INFORMATION_NODE_READER_HPP

#include <open62541/client_highlevel.h>
#include <open62541/server.h>

/**
 * @brief Helper encapsulating a UA_Variant buffer for reading node values.
 */
class information_node_reader {
private:
    UA_Variant variant_; /**< Internal variant reused across reads. */
public:
    /**
     * @brief Constructs a new information node reader object.
     * 
     */
    information_node_reader();

    /**
     * @brief Destroys the information node reader object.
     * 
     */
    ~information_node_reader();

    /**
     * @brief Reads an information node of a remote OPC UA host.
     * 
     * @param _client the client.
     * @param _node_id the node id.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    read_information_node(UA_Client* _client, UA_NodeId _node_id);

    /**
     * @brief Reads an information node from the own server address space.
     * 
     * @param _server the server.
     * @param _node_id the node id.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    read_information_node(UA_Server* _server, UA_NodeId _node_id);

    /**
     * @brief Returns the variant in which the value of the read node is stored.
     * 
     * @return UA_Variant* the variant containing the read node value.
     */
    UA_Variant*
    get_variant();
};

#endif // INFORMATION_NODE_READER_HPP