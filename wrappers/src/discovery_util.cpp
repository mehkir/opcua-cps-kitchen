#include "../include/discovery_util.hpp"
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <string>
#include "../include/client_connection_establisher.hpp"

#define DISCOVERY_SERVER_ENDPOINT "opc.tcp://localhost:4840"
#define REGISTER_RENEWAL 50
#define REGISTER_INTERVAL 5

discovery_util::discovery_util() : running_(true), discovery_connected_(false) {
}

discovery_util::~discovery_util() {
    running_ = false;
    discovery_connected_ = false;
    discovery_cv_.notify_all();
    if (discovery_thread_.joinable())
        discovery_thread_.join();
}

UA_StatusCode
discovery_util::register_server(UA_Server* _server) {
    if (!client_connection_establisher::test_connection(DISCOVERY_SERVER_ENDPOINT))
        return UA_STATUSCODE_BAD;
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);
    return UA_Server_registerDiscovery(_server, &cc, UA_STRING(const_cast<char*>(DISCOVERY_SERVER_ENDPOINT)), UA_STRING_NULL);
}

UA_StatusCode
discovery_util::deregister_server(UA_Server* _server) {
    UA_ClientConfig cc;
    memset(&cc, 0, sizeof(UA_ClientConfig));
    UA_ClientConfig_setDefault(&cc);
    return UA_Server_deregisterDiscovery(_server, &cc, UA_STRING(const_cast<char*>(DISCOVERY_SERVER_ENDPOINT)));
}

UA_StatusCode
discovery_util::lookup_endpoints(std::vector<std::string>& _endpoints, std::string _application_uri) {
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

        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Endpoint for Server[%lu]: %.*s", (unsigned long) i,
               (int) description->applicationUri.length, description->applicationUri.data);

        std::string discovery_url((char*) description->discoveryUrls[0].data, description->discoveryUrls[0].length);
        _endpoints.push_back(discovery_url);
    }

    UA_Array_delete(application_description_array, application_description_array_size,
                    &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);

    return UA_STATUSCODE_GOOD;
}

UA_StatusCode
discovery_util::register_server_repeatedly(UA_Server* _server) {
    try {
        discovery_thread_ = std::thread([this, _server]() {
            while(running_) {
                UA_StatusCode status;
                while ((status = register_server(_server)) != UA_STATUSCODE_GOOD) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error registering on discovery server. Retrying in %d seconds (%s)", __FUNCTION__, REGISTER_INTERVAL, UA_StatusCode_name(status));
                    std::this_thread::sleep_for(std::chrono::seconds(REGISTER_INTERVAL));
                    if (!running_) break;
                }
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Registered on discovery server. Renewal in %d minutes", __FUNCTION__, REGISTER_RENEWAL);
                {
                    std::unique_lock<std::mutex> lock(discovery_mutex_);
                    discovery_connected_ = true;
                    discovery_cv_.notify_all();
                    if (running_)
                        discovery_cv_.wait_for(lock, std::chrono::minutes(REGISTER_RENEWAL), [this] { return !running_ || !discovery_connected_; });
                }
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running discovery thread");
        return UA_STATUSCODE_BAD;
    }
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode
discovery_util::lookup_endpoints_repeatedly(std::vector<std::string>& _endpoints, std::string _application_uri) {
    UA_StatusCode status;
    while ((status = lookup_endpoints(_endpoints, _application_uri)) != UA_STATUSCODE_GOOD || _endpoints.empty()) {
        if (!running_) {
            return UA_STATUSCODE_BAD;
        }
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Failed looking up endpoints (%s). Waiting for reconnection to the discovery server.", __FUNCTION__, UA_StatusCode_name(status));
            {
                std::unique_lock<std::mutex> lock(discovery_mutex_);
                discovery_connected_ = false;
                discovery_cv_.notify_all();
                if (running_)
                    discovery_cv_.wait(lock, [this] { return !running_ || discovery_connected_; });
            }
            continue;
        }
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: No endpoints received. Looking up again in %d seconds", __FUNCTION__, LOOKUP_INTERVAL);
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
    }
    return UA_STATUSCODE_GOOD;
}

void
discovery_util::stop() {
    {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        running_ = false;
        discovery_connected_ = false;
        discovery_cv_.notify_all();
    }
    if (discovery_thread_.joinable())
        discovery_thread_.join();
}