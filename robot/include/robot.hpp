#ifndef ROBOT_HPP
#define ROBOT_HPP

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>

#include <signal.h>
#include <stdio.h>
#include <thread>

#include "information_node_inserter.hpp"
#include "node_value_subscriber.hpp"

class robot {
private:
    UA_Server* robot_server_;
    UA_UInt16 robot_port_;
    UA_Client* clock_client_;
    node_value_subscriber clock_tick_subscriber_;
    UA_Int64 current_clock_tick_;
    UA_Int64 next_clock_tick_;
    std::thread client_iterate_thread_;
    volatile UA_Boolean running_;

    static void
    clock_tick_notification_callback(UA_Client* client, UA_UInt32 subscription_id, void* subscription_context,
                                    UA_UInt32 monitor_id, void* monitor_context, UA_DataValue* value);
    void
    handle_clock_tick_notification(UA_UInt64 _new_clock_tick);

    static void
    receive_tick_ack_called(UA_Client* client, void* userdata, UA_UInt32 request_id, UA_CallResponse* response);

    void
    handle_receive_tick_ack(UA_Boolean _tick_ack_result);
public:
    robot(uint16_t _port, uint16_t _clock_port);
    ~robot();

    void
    start();

    void
    stop();
};


#endif // ROBOT_HPP