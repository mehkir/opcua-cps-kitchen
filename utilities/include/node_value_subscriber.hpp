#ifndef NODE_VALUE_SUBSCRIBER_HPP
#define NODE_VALUE_SUBSCRIBER_HPP

#include <open62541/client_subscriptions.h>

class node_value_subscriber {
private:
    /* data */
public:
    node_value_subscriber();
    ~node_value_subscriber();
    UA_StatusCode subscribe_node_value(UA_Client* _client, UA_NodeId _monitored_node_id, UA_Client_DataChangeNotificationCallback _notification_callback);
};


#endif // NODE_VALUE_SUBSCRIBER_HPP