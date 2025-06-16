#include "../include/callback_scheduler.hpp"

callback_scheduler::callback_scheduler(UA_Server* _server, UA_ServerCallback _callback, void* _data, UA_UInt64* _callback_id) : server_(_server), callback_(_callback), data_(_data), callback_id_(_callback_id) {
}

callback_scheduler::~callback_scheduler() {
}

UA_StatusCode 
callback_scheduler::schedule_from_now(UA_DateTime _expiry_time) {
    return UA_Server_addTimedCallback(server_, callback_, data_, _expiry_time, callback_id_);
}

UA_StatusCode
callback_scheduler::schedule_from_now_relative(UA_Double _delay_in_ms) {
    OnceCallback *once = (OnceCallback*)UA_malloc(sizeof(*once));
    if(!once) return UA_STATUSCODE_BADOUTOFMEMORY;
    once->cb = callback_;
    once->data = data_;
    return UA_Server_addRepeatedCallback(server_, once_wrapper, once, _delay_in_ms, &once->id);
}