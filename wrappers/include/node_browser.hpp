#ifndef NODE_BROWSER_HPP
#define NODE_BROWSER_HPP

#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <string>

class node_browser {
private:
public:
    node_browser();
    ~node_browser();

    /**
     * @brief Returns the node id of the object type by its browse name
     * 
     * @param _client the client
     * @param _start_node_id the start node id
     * @param _target_browsename the browse name of the object type
     * @return UA_NodeId the node id if object type is found, else UA_NODEID_NULL
     */
    UA_NodeId browse_object_type(UA_Client* _client, UA_NodeId _start_node_id, std::string _target_browsename);

    /**
     * @brief Returns all object nodes under the objects folder
     * 
     * @param _client the client
     * @param _browse_result the browse result where the search result is stored
     */
    void browse_objects(UA_Client* _client, UA_BrowseResult& _browse_result);

    /**
     * @brief Returns all methods of an object node
     * 
     * @param _client the client
     * @param _instance_id the id of the object node
     * @param _browse_result the browse result where the search result is stored
     */
    void browse_methods(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result);

    /**
     * @brief Returns all object attributes of an object
     * 
     * @param _client the client
     * @param _instance_id the object id under which to search for object attributes
     * @param _browse_result the browse result where the search result is stored
     */
    void browse_objects(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result);

    /**
     * @brief Returns all variable attributes of an object
     * 
     * @param _client the client
     * @param _instance_id the object id under which to search for variable attributes
     * @param _browse_result the browse result where the search result is stored
     */
    void browse_attributes(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result);

};

#endif // NODE_BROWSER_HPP