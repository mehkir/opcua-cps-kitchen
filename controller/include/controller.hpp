#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <open62541/server.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <thread>
#include <map>
#include <set>
#include <memory>
#include "node_value_subscriber.hpp"
#include "node_ids.hpp"
#include "method_node_caller.hpp"
#include "method_node_inserter.hpp"
#include "client_connection_establisher.hpp"
#include "information_node_inserter.hpp"
#include "types.hpp"
#include "recipe_parser.hpp"

using namespace cps_kitchen;

struct remote_robot {
    enum class state_status {
        CURRENT,
        OBSOLETE
    };

    enum class state {
        IDLING,
        COOKING,
        FINISHED,
    };

    private:
        UA_Client* client_;
        const port_t port_;
        const position_t position_;
        remote_robot::state state_;
        UA_UInt32 current_tool_;
        bool running_;
        std::thread client_thread_;
        method_node_caller receive_robot_task_caller_;
        recipe_id_t recipe_id_;
        remote_robot::state_status state_status_;

    public:
        remote_robot(port_t _port, position_t _position) :  port_(_port), position_(_position), client_(UA_Client_new()), state_status_(state_status::OBSOLETE), running_(true) {
            client_connection_establisher robot_client_connection_establisher;
            UA_SessionState session_state = robot_client_connection_establisher.establish_connection(client_, port_);
            if (session_state != UA_SESSIONSTATE_ACTIVATED) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "Error establishing robot client session");
                running_ = false;
                return;
            }

            receive_robot_task_caller_.add_input_argument(&recipe_id_, UA_TYPES_UINT32);

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

        remote_robot::state get_state() const {
            return state_;
        }

        void set_state(remote_robot::state _state) {
            state_ = _state;
        }

        UA_UInt32 get_current_tool() const {
            return current_tool_;
        }

        void set_current_tool(UA_UInt32 _current_tool) {
            current_tool_ = _current_tool;
        }

        state_status get_state_status() const {
            return state_status_;
        }

        void set_state_status(state_status _state_status) {
            state_status_ = _state_status;
        }

        void instruct(recipe_id_t _recipe_id, UA_ClientAsyncCallCallback _callback) {
            // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "remote robot %s called on port", __FUNCTION__, port_);
            recipe_id_ = _recipe_id;
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "INSTRUCTIONS: Instruct robot on position %d with port %d to cook recipe %d", position_, port_, _recipe_id);
            UA_StatusCode status = receive_robot_task_caller_.call_method_node(client_, UA_NODEID_STRING(1, const_cast<char*>(RECEIVE_TASK)), _callback, this);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error calling instruct method");
                running_ = false;
            }
        }

        static const char*
        remote_robot_state_to_string(remote_robot::state _state) {
            switch (_state) {
                case remote_robot::state::IDLING: return "IDLING";
                case remote_robot::state::COOKING: return "COOKING";
                case remote_robot::state::FINISHED: return "FINISHED";
                default: return "Unimplemented item";
            }
        }

        static const char*
        remote_robot_state_status_to_string(remote_robot::state_status _state_status) {
            switch (_state_status) {
                case remote_robot::state_status::CURRENT: return "CURRENT";
                case remote_robot::state_status::OBSOLETE: return "OBSOLETE";
                default: return "Unimplemented item";
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
    method_node_inserter receive_robot_state_inserter_;
    /* recipe related member variables */
    recipe_parser recipe_parser_;

    static UA_StatusCode
    receive_robot_state(UA_Server* _server,
            const UA_NodeId* _session_id, void* _session_context,
            const UA_NodeId* _method_id, void* _method_context,
            const UA_NodeId* _object_id, void* _object_context,
            size_t _input_size, const UA_Variant* _input,
            size_t _output_size, UA_Variant* _output);

    void
    handle_robot_state(port_t _port, position_t _position, UA_UInt32 _robot_state, UA_UInt32 _current_tool, UA_Variant* _output);

    static void
    receive_robot_task_called(UA_Client* _client, void* _userdata, UA_UInt32 _request_id, UA_CallResponse* _response);

    void
    join_threads();

public:
    controller(port_t _port);
    ~controller();

    void
    start();

    void
    stop();
};

#endif // CONTROLLER_HPP