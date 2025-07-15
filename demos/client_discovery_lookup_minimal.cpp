/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */
/**
 * This client requests all the available servers from the discovery server (see server_lds.c)
 * and then calls GetEndpoints on the returned list of servers.
 */

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>

#include <stdlib.h>
#include <stdio.h>
#include <string>

#define DISCOVERY_SERVER_ENDPOINT "opc.tcp://localhost:4840"

int main(void) {
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
        return EXIT_FAILURE;
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

        printf("\nEndpoints for Server[%lu]: %.*s\n", (unsigned long) i,
               (int) description->applicationUri.length, description->applicationUri.data);

        UA_Client *client = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));

        // char *discoveryUrl = (char *) UA_malloc(sizeof(char) * description->discoveryUrls[0].length + 1);
        // memcpy(discoveryUrl, description->discoveryUrls[0].data, description->discoveryUrls[0].length);
        // discoveryUrl[description->discoveryUrls[0].length] = '\0';

        std::string discovery_url((char*) description->discoveryUrls[0].data, description->discoveryUrls[0].length);
        UA_EndpointDescription* enpoint_array = NULL;
        size_t enpoint_array_size = 0;
        retval = UA_Client_getEndpoints(client, discovery_url.c_str(), &enpoint_array_size, &enpoint_array);
        // UA_free(discoveryUrl);
        if(retval != UA_STATUSCODE_GOOD) {
            UA_Client_disconnect(client);
            UA_Client_delete(client);
            break;
        }

        for(size_t j = 0; j < enpoint_array_size; j++) {
            UA_EndpointDescription *endpoint = &enpoint_array[j];
            printf("Endpoint URL: %.*s\n", (int) endpoint->endpointUrl.length, endpoint->endpointUrl.data);
        }

        UA_Array_delete(enpoint_array, enpoint_array_size, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);
        UA_Client_delete(client);
    }

    UA_Array_delete(application_description_array, application_description_array_size,
                    &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);

    return EXIT_SUCCESS;
}
