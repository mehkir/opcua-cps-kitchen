#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <open62541/server.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <map>
#include <unordered_set>
#include <memory>
#include "node_value_subscriber.hpp"
#include "node_ids.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "information_node_inserter.hpp"
#include "types.hpp"
#include "recipe_parser.hpp"
#include "robot_state.hpp"
#include "robot_tool.hpp"
#include "session_id.hpp"

using namespace cps_kitchen;

struct remote_robot {
    private:
        UA_Client* client_;
        const port_t port_;
        const position_t position_;
        std::unordered_set<std::string> capabilities_;
        robot_state state_;
        robot_tool last_equipped_tool_;
        bool running_;
        std::thread client_thread_;
        method_node_caller receive_robot_task_caller_;
        recipe_id_t recipe_id_;
        UA_UInt32 processed_steps_;

    public:
        /**
         * @brief Construct a new remote robot object.
         * 
         * @param _port the port of the remote robot
         * @param _position the position of the remote robot at the conveyor
         */
        remote_robot(port_t _port, position_t _position, std::unordered_set<std::string> _capabilities) :  port_(_port), position_(_position), capabilities_(_capabilities), client_(UA_Client_new()), running_(true) {
            client_connection_establisher robot_client_connection_establisher;
            UA_SessionState session_state = robot_client_connection_establisher.establish_connection(client_, port_);
            if (session_state != UA_SESSIONSTATE_ACTIVATED) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing robot client session");
                running_ = false;
                return;
            }

            receive_robot_task_caller_.add_scalar_input_argument(&recipe_id_, UA_TYPES_UINT32);
            receive_robot_task_caller_.add_scalar_input_argument(&processed_steps_, UA_TYPES_UINT32);

            client_thread_ = std::thread([this]() {
                while(running_) {
                    UA_StatusCode status = UA_Client_run_iterate(client_, 100);
                    if (status != UA_STATUSCODE_GOOD) {
                        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Error running robot client");
                        running_ = false;
                    }
                }
            });
        }

        /**
         * @brief Destroy the remote robot object.
         * 
         */
        ~remote_robot() {
            running_ = false;
            if (client_thread_.joinable())
                client_thread_.join();
            UA_Client_delete(client_);
        }

        port_t get_port() const {
            return port_;
        }

        position_t get_position() const {
            return position_;
        }

        bool is_capable_to(std::string _capability) const {
            return capabilities_.find(_capability) != capabilities_.end();
        }

        robot_tool get_last_equipped_tool() const {
            return last_equipped_tool_;
        }

        void set_last_equipped_tool(robot_tool _last_equipped_tool) {
            last_equipped_tool_ = _last_equipped_tool;
        }

        /**
         * @brief Instructs the remote robot to process a dish.
         * 
         * @param _recipe_id the recipe ID of the dish
         * @param _processed_steps the processed steps of the recipe ID so far
         * @param _callback the callback called after the robot is instructed
         */
        void instruct(recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_ClientAsyncCallCallback _callback) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            recipe_id_ = _recipe_id;
            processed_steps_ = _processed_steps;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "INSTRUCTIONS: Instruct robot on position %d with port %d to cook recipe %d from step %d", position_, port_, _recipe_id, _processed_steps);
            UA_StatusCode status = receive_robot_task_caller_.call_method_node(client_, UA_NODEID_STRING(1, const_cast<char*>(RECEIVE_TASK)), _callback, this);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
                running_ = false;
            }
        }
};

class controller {
private:
    /* controller related member variables */
    UA_Server* server_;
    port_t port_;
    volatile UA_Boolean running_;
    std::thread server_iterate_thread_;
    /* robot related member variables */
    std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>> position_remote_robot_map_;
    method_node_inserter choose_next_robot_inserter_;
    method_node_inserter register_robot_inserter_;
    /* recipe related member variables */
    recipe_parser recipe_parser_;

    /**
     * @brief Register robot.
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
     * @return UA_StatusCode 
     */
    static UA_StatusCode
    register_robot(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Registers a remote robot.
     * 
     * @param _port the port of the remote robot
     * @param _position the position of the remote robot
     * @param _remote_robot_capabilities the capabilities of the remote robot
     * @param _output the output pointer to store return parameters
     */
    void
    handle_robot_registration(port_t _port, position_t _position, std::unordered_set<std::string> _remote_robot_capabilities, UA_Variant* _output);

    /**
     * @brief Extracts the received robot and recipe parameters.
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
     * @return UA_StatusCode 
     */
    static UA_StatusCode
    choose_next_robot(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    /**
     * @brief Chooses the next suitable robot
     * 
     * @param _port the port of the remote robot
     * @param _position the position of the remote robot
     * @param _recipe_id the recipe id of the partial finished order
     * @param _processed_steps the steps until the recipe is processed
     */
    void
    handle_next_robot_request(port_t _port, position_t _position, recipe_id_t _recipe_id, UA_UInt32 _processed_steps, UA_Variant* _output);

    /**
     * @brief Callback called after robot is instructed. Extracts the returned robot state parameters.
     * 
     * @param _client the client instance from which this method is called
     * @param _userdata the userdata passed to the instruct call
     * @param _request_id 
     * @param _response the pointer to the returned parameters
     */
    static void
    receive_robot_task_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    /**
     * @brief Joins all started threads.
     * 
     */
    void
    join_threads();

public:
    /**
     * @brief Construct a new controller object.
     * 
     * @param _port the controller port
     */
    controller(port_t _port);

    /**
     * @brief Destroy the controller object.
     * 
     */
    ~controller();

    /**
     * @brief Checks if initialization was successful and joins all started threads.
     * 
     */
    void
    start();

    /**
     * @brief Stops the controller and shuts it down.
     * 
     */
    void
    stop();
};

#endif // CONTROLLER_HPP