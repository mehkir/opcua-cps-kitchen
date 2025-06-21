#ifndef CALLBACK_SCHEDULER_HPP
#define CALLBACK_SCHEDULER_HPP

#include <open62541/server.h>

typedef struct {
    UA_ServerCallback cb;
    void *data;
    UA_UInt64 id;
} once_callback;

static void once_wrapper(UA_Server* _server, void* _context) {
    once_callback *once = (once_callback*)_context;
    once->cb(_server, once->data);
    UA_Server_removeRepeatedCallback(_server, once->id);
    UA_free(once);
}

class callback_scheduler
{
private:
    UA_Server* server_;
    UA_ServerCallback callback_;
    void* data_;
    UA_UInt64* callback_id_;
public:
    callback_scheduler(UA_Server* _server, UA_ServerCallback _callback, void* _data, UA_UInt64* _callback_id);
    ~callback_scheduler();
    UA_StatusCode schedule_from_now(UA_DateTime _expiry_time);
    UA_StatusCode schedule_from_now_relative(UA_Double _delay_in_ms);
};

#endif // CALLBACK_SCHEDULER_HPP