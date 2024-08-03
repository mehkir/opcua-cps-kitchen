#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <open62541/client.h>
#include <thread>
#include <unordered_map>
#include "node_value_subscriber.hpp"
#include "node_ids.hpp"
#include "method_node_caller.hpp"


struct remote_robot {
    private:
        UA_Client* client_;
        uint16_t port_;
        bool busy_;
        node_value_subscriber status_subscriber_;

        static void
        status_notification_callback(UA_Client* client, UA_UInt32 subscription_id, void* subscription_context,
                                    UA_UInt32 monitor_id, void* monitor_context, UA_DataValue* value) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
            if(UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
                UA_Boolean new_status = *(UA_Boolean*) value->value.data;
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                    "New status is: %lu", new_status);

                remote_robot* self = static_cast<remote_robot*>(monitor_context);
                self->handle_status_notification(new_status);
            }
        }

        void
        handle_status_notification(UA_Boolean _new_status) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s", __FUNCTION__);
            busy_ = _new_status;
        }

    public:
        remote_robot(UA_Client* _client, uint16_t _port) :  client_(_client), port_(_port), busy_(false) {
            UA_StatusCode status = status_subscriber_.subscribe_node_value(client_, UA_NODEID_STRING(1, ROBOT_STATUS), status_notification_callback, this);
            if(status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error subscribing to the robot status node");
            }
        }

        ~remote_robot() {
            UA_Client_delete(client_);
        }

        UA_Client* get_client() {
            return client_;
        }

        uint16_t get_port() {
            return port_;
        }

        bool is_busy() {
            return busy_;
        }
};

class controller {
private:
    UA_UInt16 controller_port_;
    std::unordered_map<uint16_t, remote_robot> port_remote_robot_map_;
    std::unordered_map<uint16_t, std::thread> port_client_thread_map_;
    volatile UA_Boolean running_;
    UA_Client* clock_client_;
    UA_Int64 current_clock_tick_;
    UA_Int64 next_clock_tick_;
    node_value_subscriber clock_tick_subscriber_;
    method_node_caller receive_tick_ack_caller_;

    static void
    clock_tick_notification_callback(UA_Client* client, UA_UInt32 subscription_id, void* subscription_context,
                                    UA_UInt32 monitor_id, void* monitor_context, UA_DataValue* value);
    void
    handle_clock_tick_notification(UA_UInt64 _new_clock_tick);

    static void
    receive_tick_ack_called(UA_Client* client, void* userdata, UA_UInt32 request_id, UA_CallResponse* response);

    void
    handle_receive_tick_ack_result(UA_Boolean _tick_ack_result);
public:
    controller(uint16_t _controller_port, uint16_t _start_port, uint32_t _robot_count, uint16_t _clock_port);
    ~controller();

    void
    start();

    void
    stop();
};

#endif // CONTROLLER_HPP