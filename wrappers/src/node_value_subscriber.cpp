#include "../include/node_value_subscriber.hpp"
#include <open62541/plugin/log_stdout.h>

node_value_subscriber::node_value_subscriber(UA_Client* _client) : client_(_client), subscription_id_(0) {

}

node_value_subscriber::~node_value_subscriber() {
    UA_Client_Subscriptions_deleteSingle(client_, subscription_id_);
}

UA_StatusCode node_value_subscriber::subscribe_node_value(UA_NodeId _monitored_node_id, UA_Client_DataChangeNotificationCallback _notification_callback, void* _context) {
    if (client_ == nullptr)
        return UA_STATUSCODE_BAD;
    /* Create a subscription */
    if (subscription_id_ == 0) {
        UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
        request.requestedPublishingInterval = 0.0;
        UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client_, request, NULL, NULL, NULL);

        if(response.responseHeader.serviceResult != UA_STATUSCODE_GOOD)
            return response.responseHeader.serviceResult;
        subscription_id_ = response.subscriptionId;
    }

    /* Add a MonitoredItem */
    UA_MonitoredItemCreateRequest monitor_request = UA_MonitoredItemCreateRequest_default(_monitored_node_id);
    monitor_request.monitoringMode = UA_MONITORINGMODE_REPORTING;
    monitor_request.requestedParameters.samplingInterval = 0.0;

    UA_MonitoredItemCreateResult monitor_response = UA_Client_MonitoredItems_createDataChange(client_, subscription_id_,
                                                    UA_TIMESTAMPSTORETURN_BOTH, monitor_request,
                                                    _context, _notification_callback, NULL);
    return monitor_response.statusCode;
}
