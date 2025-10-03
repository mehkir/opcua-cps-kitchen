/**
 * @file callback_scheduler.hpp
 * @brief Utilities to schedule one-shot and repeated callbacks.
 *
 * Provides a lightweight RAII style helper to schedule server callbacks relative
 * to the current server time or with an absolute expiry. Also includes an internal
 * wrapper used to implement one-shot execution semantics on top of repeated callbacks.
 */
#ifndef CALLBACK_SCHEDULER_HPP
#define CALLBACK_SCHEDULER_HPP

#include <open62541/server.h>

/**
 * @brief Internal structure representing a scheduled one-shot callback.
 *
 * The callback is implemented by registering a repeated callback and removing
 * it manually after the first invocation.
 */
typedef struct {
    UA_ServerCallback cb;  /**< User supplied callback. */
    void *data;            /**< User data passed to callback. */
    UA_UInt64 id;          /**< Assigned repeated callback id. */
} once_callback;

/**
 * @brief Wrapper that forwards to the user callback and removes the repeated callback.
 * @internal
 */
static void
once_wrapper(UA_Server* _server, void* _context) {
    once_callback *once = (once_callback*)_context;
    once->cb(_server, once->data);
    UA_Server_removeRepeatedCallback(_server, once->id);
    UA_free(once);
}

/**
 * @brief Schedules a server callback either at an absolute time or relative delay.
 *
 * Instances are lightweight holders for parameters required to schedule a callback.
 * The created callback id is written back through the pointer supplied to the
 * constructor to allow later cancellation by the caller.
 */
class callback_scheduler {
private:
    UA_Server* server_;          /**< Target OPC UA server instance. */
    UA_ServerCallback callback_; /**< User supplied callback. */
    void* data_;                 /**< Opaque user data passed to callback. */
    UA_UInt64* callback_id_;     /**< Out parameter storing created callback id. */
public:
    /**
     * @brief Construct a scheduler for a server callback.
     * 
     * @param _server Server pointer (not owned).
     * @param _callback Callback to execute.
     * @param _data User data forwarded to callback.
     * @param _callback_id Pointer where the created callback id will be stored.
     */
    callback_scheduler(UA_Server* _server, UA_ServerCallback _callback, void* _data, UA_UInt64* _callback_id);
    /**
     * @brief Destructor (does not cancel any scheduled callback).
     */
    ~callback_scheduler();
    /**
     * @brief Schedule callback relative to now using an absolute expiry time.
     * 
     * @param _expiry_time Absolute expiry (server DateTime) at which callback fires.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    schedule_from_now(UA_DateTime _expiry_time);

    /**
     * @brief Schedule callback after a relative delay.
     * 
     * @param _delay_in_ms Delay in milliseconds.
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    schedule_from_now_relative(UA_Double _delay_in_ms);
};

#endif // CALLBACK_SCHEDULER_HPP