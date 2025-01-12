#ifndef CALLBACK_SCHEDULER_HPP
#define CALLBACK_SCHEDULER_HPP

#include <open62541/server.h>

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
};

#endif // CALLBACK_SCHEDULER_HPP