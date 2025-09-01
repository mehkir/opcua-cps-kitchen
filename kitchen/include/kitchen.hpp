#ifndef KITCHEN_HPP
#define KITCHEN_HPP

#define REMOTE_ROBOT_INSTANCE_NAME(POS) "RemoteKitchenRobot" #POS

#include <open62541/server.h>
#include <open62541/client.h>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <random>

#include "object_type_node_inserter.hpp"
#include "node_browser_helper.hpp"
#include "discovery_util.hpp"
#include "browsenames.h"
#include "types.hpp"

using namespace cps_kitchen;

struct remote_robot {
    private:
        UA_Client* client_;
        std::string endpoint_;
        position_t position_;
        std::atomic<bool> running_;
        std::thread client_iterate_thread_;
        std::unordered_map<std::string, object_method_info> method_id_map_;
        object_type_node_inserter& remote_robot_type_inserter_;
    public:
        /**
         * @brief Setup the remote robot object type
         * 
         * @param _remote_robot_type_inserter the remote robot type inserter
         * @param _conveyor the conveyor server
         * @return UA_StatusCode the status code
         */
        static UA_StatusCode setup_remote_robot_object_type(object_type_node_inserter& _remote_robot_type_inserter, UA_Server* _kitchen) {
            UA_StatusCode status;
            /* Add attributes */
            status = _remote_robot_type_inserter.add_attribute(REMOTE_ROBOT_TYPE, POSITION);
            status |= _remote_robot_type_inserter.add_attribute(REMOTE_ROBOT_TYPE, CONNECTIVITY);
            /* Add remote robot type constructor */
            status |= _remote_robot_type_inserter.add_object_type_constructor(_kitchen, _remote_robot_type_inserter.get_object_type_id(REMOTE_ROBOT_TYPE));
            return status;
        }

        /**
         * @brief Construct a new remote robot object.
         * 
         * @param _endpoint the robot's endpoint url
         * @param _position the position of the remote robot at the conveyor
         * @param _kitchen_instance_id the kitchen instance id
         * @param _remote_robot_type_inserter the remote robot type inserter
         */
        remote_robot(std::string _endpoint, position_t _position, UA_NodeId _kitchen_instance_id, object_type_node_inserter& _remote_robot_type_inserter) : client_(nullptr), endpoint_(_endpoint), position_(_position), running_(true), remote_robot_type_inserter_(_remote_robot_type_inserter) {
            /* Instantiate remote robot type */
            UA_StatusCode status = remote_robot_type_inserter_.add_object_instance(REMOTE_ROBOT_INSTANCE_NAME(_position), PLATE_TYPE, _kitchen_instance_id, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding plate object instance");
                return;
            }
            /* Set attribute values */
            status = remote_robot_type_inserter_.set_scalar_attribute(REMOTE_ROBOT_INSTANCE_NAME(_position), POSITION, &position_, UA_TYPES_UINT32);
            status |= remote_robot_type_inserter_.set_scalar_attribute(REMOTE_ROBOT_INSTANCE_NAME(_position), CONNECTIVITY, &position_, UA_TYPES_UINT32);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting plate attributes");
            }
            // TODO: Setup connection and add remove marked robots callback and make server delete node work
        }

        /**
         * @brief Destroy the remote robot object.
         * 
         */
        ~remote_robot() {
            running_ = false;
            if (client_iterate_thread_.joinable())
                client_iterate_thread_.join();
            // UA_Server_deleteNode(kitchen_, instance_id, true);
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
    std::unordered_map<std::string, object_method_info> method_id_map_;
    std::thread server_iterate_thread_;
    std::mutex client_mutex_;
    std::thread client_iterate_thread_;
    /* remote robot related member variables */
    std::unordered_map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_;
    object_type_node_inserter remote_robot_type_inserter_;
    /* controller related member variables */
    UA_Client* controller_client_;
    object_type_node_inserter remote_controller_type_inserter_;
    /* conveyor related member variables */
    UA_Client* conveyor_client_;
    object_type_node_inserter remote_conveyor_type_inserter_;
    /* random distribution */
    std::random_device random_device_;
    std::mt19937 mersenne_twister_;
    std::uniform_int_distribution<std::uint32_t> uniform_int_distribution_;

    /**
     * @brief Places a random order.
     * 
     * @param _server the server instance from which this method is called
     * @param _session_id 
     * @param _session_context 
     * @param _method_id 
     * @param _method_context the node context data passed to the method node
     * @param _object_id 
     * @param _object_context 
     * @param _input_size the count of the input parameters
     * @param _input the input pointer of the input parameters
     * @param _output_size the allocated output size
     * @param _output the output pointer to store return parameters
     * @return UA_StatusCode the status code in the response
     */
    static UA_StatusCode
    place_random_order(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Handles the random order request.
     * 
     * @param _output the output pointer to store return parameters
     */
    void
    handle_random_order_request(UA_Variant* _output);

    /**
     * @brief Extracts the returned robot state parameters.
     * 
     * @param _output_size the count of returned output values
     * @param _output the variant containing the output values
     */
    void
    receive_robot_task_called(size_t _output_size, UA_Variant* _output);

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