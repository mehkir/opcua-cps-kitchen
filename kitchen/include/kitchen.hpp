#ifndef KITCHEN_HPP
#define KITCHEN_HPP

#include <open62541/server.h>
#include <open62541/client.h>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <memory>

#include "object_type_node_inserter.hpp"
#include "node_browser_helper.hpp"
#include "discovery_util.hpp"
#include "types.hpp"

using namespace cps_kitchen;

struct remote_robot {
    private:
        UA_Client* client_;
        std::string endpoint_;
        const position_t position_;
        std::atomic<bool> running_;
        std::thread client_iterate_thread_;
        std::unordered_map<std::string, object_method_info> method_id_map_;
    public:
        /**
         * @brief Construct a new remote robot object.
         * 
         * @param _endpoint the robot's endpoint url
         * @param _position the position of the remote robot at the conveyor
         */
        remote_robot(std::string _endpoint, position_t _position) : client_(nullptr), endpoint_(_endpoint), position_(_position), running_(true) {
            // TODO
        }

        /**
         * @brief Destroy the remote robot object.
         * 
         */
        ~remote_robot() {
            running_ = false;
            if (client_iterate_thread_.joinable())
                client_iterate_thread_.join();
            UA_Client_delete(client_);
        }
};

class kitchen {

private:
    /* kitchen related member variables */
    UA_Server* server_;
    std::string kitchen_uri_;
    object_type_node_inserter kitchen_type_inserter_;
    std::atomic<bool> running_;
    discovery_util discovery_util_;
    std::thread server_iterate_thread_;
    std::mutex client_mutex_;
    std::thread client_iterate_thread_;
    /* robot related member variables */
    std::unordered_map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_;
    /* controller related member variables */
    UA_Client* controller_client_;
    /* conveyor related member variables */
    UA_Client* conveyor_client_;

public:
    kitchen();
    ~kitchen();

    /**
     * @brief Starts the kitchen and joins all started threads.
     * 
     */
    void start();

    /**
     * @brief Stops the kitchen and shuts it down.
     * 
     */
    void stop();
};

#endif // KITCHEN_HPP