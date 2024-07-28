#ifndef CLOCK_HPP
#define CLOCK_HPP

#include <open62541/server.h>
#include <set>

#include "information_node_inserter.hpp"
#include "method_node_inserter.hpp"

class tick_clock {
private:
    UA_Server* clock_server_;
    UA_Int16 clock_port_;
    UA_UInt64 clock_tick_;
    UA_UInt64 next_clock_tick_;
    UA_UInt32 clock_client_count_;
    std::set<uint16_t> currently_acknowledged_set_;
    information_node_inserter clock_tick_inserter_;
    method_node_inserter receive_tick_ack_inserter_;
    volatile UA_Boolean running_;

    static UA_StatusCode
    receive_tick_ack (UA_Server *server,
            const UA_NodeId *session_id, void *session_context,
            const UA_NodeId *method_id, void *method_context,
            const UA_NodeId *object_id, void *object_context,
            size_t input_size, const UA_Variant *input,
            size_t output_size, UA_Variant *output);

    void
    handle_receive_tick_ack(UA_UInt64 _current_client_tick, UA_UInt64 _next_tick, UA_UInt16 _port, UA_Variant* output);
public:
    tick_clock(UA_UInt16 _clock_port, UA_UInt32 _clock_client_count);
    ~tick_clock();

    void
    start();

    void
    stop();
};

#endif // CLOCK_HPP