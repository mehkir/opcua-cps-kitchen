#include "../include/callback_scheduler.hpp"

callback_scheduler::callback_scheduler(UA_Server* _server, UA_ServerCallback _callback, void* _data, UA_UInt64* _callback_id) : server_(_server), callback_(_callback), data_(_data), callback_id_(_callback_id) {
}

callback_scheduler::~callback_scheduler() {
}

UA_StatusCode 
callback_scheduler::schedule_from_now(UA_DateTime _expiry_time) {
    return UA_Server_addTimedCallback(server_, callback_, data_, _expiry_time, callback_id_);
}