#include "../include/node_value_subscriber.hpp"
#include <open62541/plugin/log_stdout.h>

#define RETRY_LIMIT 10

node_value_subscriber::node_value_subscriber() {

}

node_value_subscriber::~node_value_subscriber() {

}

UA_StatusCode node_value_subscriber::subscribe_node_value_(UA_Client* _client, UA_NodeId _monitored_node_id, UA_Client_DataChangeNotificationCallback _notification_callback, void* _context) {
    /* Create a subscription */
    UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
    request.requestedPublishingInterval = 0.0;
    UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(_client, request, NULL, NULL, NULL);

    if(response.responseHeader.serviceResult != UA_STATUSCODE_GOOD)
        return UA_STATUSCODE_BAD;

    /* Add a MonitoredItem */
    UA_MonitoredItemCreateRequest monitor_request = UA_MonitoredItemCreateRequest_default(_monitored_node_id);
    monitor_request.monitoringMode = UA_MONITORINGMODE_REPORTING;
    monitor_request.requestedParameters.samplingInterval = 0.0;

    UA_MonitoredItemCreateResult monitor_response = UA_Client_MonitoredItems_createDataChange(_client, response.subscriptionId,
                                                    UA_TIMESTAMPSTORETURN_BOTH, monitor_request,
                                                    _context, _notification_callback, NULL);
    return monitor_response.statusCode;
}

UA_StatusCode node_value_subscriber::subscribe_node_value(UA_Client* _client, UA_NodeId _monitored_node_id, UA_Client_DataChangeNotificationCallback _notification_callback, void* _context) {
    int retry_counter = 0;
    UA_StatusCode status;
    while ((retry_counter < RETRY_LIMIT) && ((status = subscribe_node_value_(_client, _monitored_node_id, _notification_callback, _context)) != UA_STATUSCODE_GOOD)) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Not subscribed. Retrying to subscribe in 1 second");
        retry_counter++;
        // UA_sleep_ms(1000);
    }
    return status;
}