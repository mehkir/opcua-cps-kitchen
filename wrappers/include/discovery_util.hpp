#ifndef DISCOVERY_UTIL_HPP
#define DISCOVERY_UTIL_HPP
#define LOOKUP_INTERVAL 5

#include <open62541/server_config_default.h>
#include <vector>
#include <string>
#include <thread>
#include <condition_variable>
#include <atomic>

class discovery_util {
private:
    std::thread discovery_thread_;
    std::condition_variable discovery_cv_;
    std::mutex discovery_mutex_;
    std::atomic<bool> running_;
    std::atomic<bool> discovery_connected_;
public:
    /**
     * @brief Registers the server on the discovery server
     * 
     * @param _server the server
     * @return UA_StatusCode the status code
     */
    UA_StatusCode
    register_server(UA_Server* _server);

    /**
     * @brief Deregisters the server on the discovery server
     * 
     * @param _server the server
     * @return UA_StatusCode the status code
     */
    UA_StatusCode
    deregister_server(UA_Server* _server);

    /**
     * @brief Looks the registered server endpoints on the discovery server up
     * 
     * @param _endpoints stores the returned endpoints
     * @param _application_uri the application uri to filter endpoints by. If empty, all server endpoints are returned
     * @return UA_StatusCode the status code
     */
    UA_StatusCode
    lookup_endpoints(std::vector<std::string>& _endpoints, std::string _application_uri = "");

    /**
     * @brief Registers the server repeatedly
     * 
     * @param _server the server
     * @return UA_StatusCode the status code
     */
    UA_StatusCode
    register_server_repeatedly(UA_Server* _server);

    /**
     * @brief Looks the registered server endpoints on the discovery server up repeatedly
     * 
     * @param _endpoints stores the returned endpoints
     * @param _application_uri the application uri to filter endpoints by. If empty, all server endpoints are returned
     * @return UA_StatusCode the status code
     */
    UA_StatusCode
    lookup_endpoints_repeatedly(std::vector<std::string>& _endpoints, std::string _application_uri = "");

    /**
     * @brief Stops the discovery thread and waits for its exit
     * 
     */
    void
    stop();

    discovery_util();
    ~discovery_util();
};


#endif // DISCOVERY_UTIL_HPP