/**
 * @file node_value_subscriber.hpp
 * @brief Subscription helper for monitoring value changes of variable nodes.
 */
#ifndef NODE_VALUE_SUBSCRIBER_HPP
#define NODE_VALUE_SUBSCRIBER_HPP

#include <open62541/client_subscriptions.h>

/**
 * @brief Encapsulates subscription creation for monitoring a single node's value.
 */
class node_value_subscriber {
private:
public:
    /**
     * @brief Constructs a new node value subscriber object.
     * 
     */
    node_value_subscriber();

    /**
     * @brief Destroys the node value subscriber object.
     * 
     */
    ~node_value_subscriber();

    /**
     * @brief Subscribes to value changes.
     * @param _client the client.
     * @param _monitored_node_id the node to monitor.
     * @param _notification_callback callback invoked on data change.
     * @param _context user context pointer.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    subscribe_node_value(UA_Client* _client, UA_NodeId _monitored_node_id, UA_Client_DataChangeNotificationCallback _notification_callback, void* _context);
};


#endif // NODE_VALUE_SUBSCRIBER_HPP