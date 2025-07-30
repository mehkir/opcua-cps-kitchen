#include "../include/discovery_util.hpp"
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <string>

#define DISCOVERY_SERVER_ENDPOINT "opc.tcp://localhost:4840"

UA_StatusCode
register_server(UA_Server* _server) {
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);
    return UA_Server_registerDiscovery(_server, &cc, UA_STRING(const_cast<char*>(DISCOVERY_SERVER_ENDPOINT)), UA_STRING_NULL);
}

UA_StatusCode
deregister_server(UA_Server* _server) {
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);
    return UA_Server_deregisterDiscovery(_server, &cc, UA_STRING(const_cast<char*>(DISCOVERY_SERVER_ENDPOINT)));
}

UA_StatusCode
lookup_endpoints(std::vector<std::string>& _endpoints, std::string _application_uri) {
    /* Example for calling FindServers */
    UA_ApplicationDescription* application_description_array = NULL;
    size_t application_description_array_size = 0;

    UA_StatusCode retval;
    {
        UA_Client* client = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));
        retval = UA_Client_findServers(client, DISCOVERY_SERVER_ENDPOINT, 0, NULL, 0, NULL,
                                       &application_description_array_size, &application_description_array);
        UA_Client_delete(client);
    }
    if(retval != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "Could not call FindServers service. "
                "Is the discovery server started? StatusCode %s", UA_StatusCode_name(retval));
        return retval;
    }

    /*
     * Now that we have the list of available servers, call get endpoints on all of them
     */
    for(size_t i = 0; i < application_description_array_size; i++) {
        UA_ApplicationDescription* description = &application_description_array[i];
        if(description->discoveryUrlsSize == 0) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT,
                        "[GetEndpoints] Server %.*s did not provide any discovery urls. Skipping.",
                        (int)description->applicationUri.length, description->applicationUri.data);
            continue;
        }

        if(description->applicationType != UA_APPLICATIONTYPE_SERVER)
            continue;

        UA_UriString application_uri = UA_STRING_ALLOC(_application_uri.c_str());
        if(!_application_uri.empty() && !UA_UriString_equal(&application_uri, &description->applicationUri)) {
            UA_String_clear(&application_uri);
            continue;
        }
        UA_String_clear(&application_uri);

        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Endpoints for Server[%lu]: %.*s", (unsigned long) i,
               (int) description->applicationUri.length, description->applicationUri.data);

        UA_Client *client = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));

        std::string discovery_url((char*) description->discoveryUrls[0].data, description->discoveryUrls[0].length);
        UA_EndpointDescription* endpoint_array = NULL;
        size_t endpoint_array_size = 0;
        retval = UA_Client_getEndpoints(client, discovery_url.c_str(), &endpoint_array_size, &endpoint_array);
        if(retval != UA_STATUSCODE_GOOD) {
            UA_Client_disconnect(client);
            UA_Client_delete(client);
            if(endpoint_array)
                UA_Array_delete(endpoint_array, endpoint_array_size, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "GetEndpoints failed for server %.*s: %s",
                (int)description->applicationUri.length, description->applicationUri.data, UA_StatusCode_name(retval));
            continue;
        }

        for(size_t j = 0; j < endpoint_array_size; j++) {
            UA_EndpointDescription *endpoint = &endpoint_array[j];
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Endpoint URL: %.*s\n", (int) endpoint->endpointUrl.length, endpoint->endpointUrl.data);
            _endpoints.push_back(std::string((char*) endpoint->endpointUrl.data, endpoint->endpointUrl.length));
        }
        
        UA_Array_delete(endpoint_array, endpoint_array_size, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);
        UA_Client_delete(client);
    }

    UA_Array_delete(application_description_array, application_description_array_size,
                    &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);

    return UA_STATUSCODE_GOOD;
}