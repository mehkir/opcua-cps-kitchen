/**
 * @file event_collector.hpp
 * @brief OPC UA based event collector that monitors robot and kitchen events and outputs them to csv files for analysis.
 *
 * @details
 * Uses OPC UA discovery to find robot and kitchen endpoints, establishes per-agent client sessions, and
 * subscribes to relevant monitored items (e.g., position, robot state). Received events and value changes
 * are written as timestamped CSV records (one or more files) to support offline analysis and
 * visualization. The implementation uses an io_context-driven worker thread and per-agent subscribers to
 * manage network I/O and clean shutdown.
 */
#ifndef EVENT_COLLECTOR_HPP
#define EVENT_COLLECTOR_HPP

#include <open62541/plugin/log_stdout.h>
#include <boost/asio.hpp>
#include <map>
#include "discovery_util.hpp"
#include "types.hpp"
#include "node_browser_helper.hpp"
#include "node_value_subscriber.hpp"
#include "client_connection_establisher.hpp"
#include "browsenames.h"
#include "robot_state.hpp"
#include "robot_timestamp_recorder.hpp"

using namespace cps_kitchen;


/**
 * @brief Remote robot client to monitor kitchen robot attributes.
 * 
 */
struct remote_robot {
    private:
        UA_Client* client_; /**< the OPC UA remote robot client pointer. */
        std::string endpoint_; /**< the endpoint address. */
        std::atomic<position_t> position_; /**< the position on the conveyor belt. */
        std::unique_ptr<node_value_subscriber> nv_subscriber_; /**< the node value subscriber. */
        std::atomic<robot_state> state_; /**< the robot's state. */
        std::atomic<bool> running_; /**< flag to indicate whether the client thread should run. */
        std::thread client_iterate_thread_; /**< the client iteration thread. */
        std::mutex client_mutex_; /**< the mutex to synchronize client method calls. */
        robot_timestamp_recorder timestamp_recorder_; /**< the timestamp recorder reference to record robot state changes. */

    public:
        /**
         * @brief Constructs a new remote robot object.
         * 
         * @param _endpoint the robot's endpoint url.
         * @param _position the position of the remote robot at the conveyor.
         */
        remote_robot(std::string _endpoint, position_t _position) :
                    endpoint_(_endpoint), position_(_position), client_(nullptr),
                    running_(true), timestamp_recorder_(_endpoint) {
        }

        /**
         * @brief Initializes and starts this remote robot.
         * 
         * @return UA_StatusCode the status code.
         */
        UA_StatusCode
        initialize_and_start() {
            if (client_ != nullptr) {
                return running_.load() ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BAD;
            }
            client_connection_establisher robot_connection_establisher;
            bool connected = robot_connection_establisher.establish_connection(client_, endpoint_);
            if (!connected) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error establishing robot client session for position %d", position_.load());
                return UA_STATUSCODE_BAD;
            }

            UA_NodeId position_id = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, POSITION);
            if (UA_NodeId_equal(&position_id, &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
                return UA_STATUSCODE_BAD;
            }
            nv_subscriber_ = std::make_unique<node_value_subscriber>(client_);
            UA_StatusCode status = nv_subscriber_->subscribe_node_value(position_id, position_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, POSITION, position_.load());
                return UA_STATUSCODE_BAD;
            }

            UA_NodeId state_id = node_browser_helper().get_attribute_id(client_, ROBOT_TYPE, ROBOT_STATE);
            if (UA_NodeId_equal(&state_id, &UA_NODEID_NULL)) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, ROBOT_STATE);
                return UA_STATUSCODE_BAD;
            }
            status = nv_subscriber_->subscribe_node_value(state_id, robot_state_changed, this);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error subscribing to remote robot's %s at position %d", __FUNCTION__, ROBOT_STATE, position_.load());
                return UA_STATUSCODE_BAD;
            }
            
            try {
                client_iterate_thread_ = std::thread([this]() {
                    while(running_) {
                        {
                            std::lock_guard<std::mutex> lock(client_mutex_);
                            UA_StatusCode status = UA_Client_run_iterate(client_, 1);
                            if (status != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running robot client at position %d (%s)", __FUNCTION__, position_.load(), UA_StatusCode_name(status));
                                running_.store(false);
                                return UA_STATUSCODE_BAD;
                            }
                        }
                        if (usleep(1*1000)) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at robot client iterate sleep", __FUNCTION__);
                            running_.store(false);
                            return UA_STATUSCODE_BAD;
                        }
                        // UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Starting the next client iterate", __FUNCTION__);
                    }
                    return UA_STATUSCODE_BAD;
                });
            } catch (...) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the robot client iterate thread at position %d", __FUNCTION__, position_.load());
                running_.store(false);
                return UA_STATUSCODE_BAD;
            }
            return UA_STATUSCODE_GOOD;
        }

        /**
         * @brief Destroys the remote robot object.
         * 
         */
        ~remote_robot() {
            running_.store(false);
            if (client_iterate_thread_.joinable())
                client_iterate_thread_.join();
            timestamp_recorder_.write_timestamps();
            nv_subscriber_.reset();
            UA_Client_delete(client_);
        }

        /**
         * @brief Returns the robot's endpoint.
         * 
         * @return std::string the endpoint url.
         */
        std::string
        get_endpoint() const {
            return endpoint_;
        }

        /**
         * @brief Returns the remote robot's position.
         * 
         * @return position_t the remote robot position.
         */
        position_t
        get_position() const {
            return position_.load();
        }

    private:
        /**
         * @brief The position changed callback for the subscription.
         * 
         * @param _client the client issuing the subscription.
         * @param _sub_id server-assigned subscription id that delivered this notification.
         * @param _sub_context user-defined context data passed when creating the subscription.
         * @param _mon_id server-assigned MonitoredItemId that produced the data change.
         * @param _mon_context user-defined context data passed when creating the monitored item.
         * @param _value the reported UA_DataValue.
         */
        static void
        position_changed(UA_Client* _client, UA_UInt32 _sub_id, void* _sub_context,
            UA_UInt32 _mon_id, void* _mon_context, UA_DataValue* _value) {
            if(_mon_context == NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Monitor context is NULL", __FUNCTION__);
                return;
            }
            remote_robot* self = static_cast<remote_robot*>(_mon_context);
            if (!UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT32])) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
                self->running_.store(false);
                return;
            }
            self->position_.store(*(UA_UInt32*) _value->value.data);
            // UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's position changed from %d to %d", __FUNCTION__, old_position, self->position_.load());
        }

        /**
         * @brief The robot state changed callback for the subscription.
         * 
         * @param _client the client issuing the subscription.
         * @param _sub_id server-assigned subscription id that delivered this notification.
         * @param _sub_context user-defined context data passed when creating the subscription.
         * @param _mon_id server-assigned MonitoredItemId that produced the data change.
         * @param _mon_context user-defined context data passed when creating the monitored item.
         * @param _value the reported UA_DataValue.
         */
        static void
        robot_state_changed(UA_Client* _client, UA_UInt32 _sub_id, void* _sub_context,
            UA_UInt32 _mon_id, void* _mon_context, UA_DataValue* _value) {
            if(_mon_context == NULL) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Monitor context is NULL", __FUNCTION__);
                return;
            }
            remote_robot* self = static_cast<remote_robot*>(_mon_context);
            if (!UA_Variant_hasScalarType(&_value->value, &UA_TYPES[UA_TYPES_UINT32])) {
                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
                self->running_.store(false);
                return;
            }
            self->state_.store(*(robot_state*) _value->value.data);
            self->timestamp_recorder_.record_timestamp(self->position_.load(), self->state_.load());
            // UA_LOG_INFO(APP_LOGGER, UA_LOGCATEGORY_USERLAND, "%s: Remote robot's state at position %d is %s", __FUNCTION__, self->position_.load(), robot_state_to_string(self->state_.load()).c_str());
        }

        /**
         * @brief Indicates whether the robot is stopped and not running anymore.
         * 
         * @return true if robot is stopped.
         * @return false if robot is still running.
         */
        bool
        is_stopped() {
            return !running_.load();
        }
};


class event_collector {
private:
    /* event collector related member variables */
    std::atomic<bool> stopped_; /**< flag to indicate whether the event collector has already called stop(). */
    discovery_util discovery_util_; /**< the discovery utility. */
    std::thread worker_thread_; /**< the worker thread. */
    boost::asio::io_context io_context_; /**< the io context managing the worker thread. */
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type, void, void> work_guard_; /**< the work guard for the io_context_. */
    boost::asio::steady_timer steady_timer_; /**< the steady timer for action time simulation. */
    /* robot related member variables */
    std::map<position_t, std::unique_ptr<remote_robot>> position_remote_robot_map_; /**< the map holding the remote robot instances. */

public:
    event_collector(/* args */);
    ~event_collector();

    /**
     * @brief Discovers the agents to be monitored.
     * 
     */
    void
    discover_agents();

    /**
     * @brief Schedules the next agents discovery.
     * 
     */
    void
    schedule_next_agents_discovery();

    /**
     * @brief Handles the discovered robot with the given endpoint.
     * 
     * @param _endpoint the discovered robot's endpoint.
     */
    void
    handle_discovered_robot(std::string _endpoint);

    /**
     * @brief Join worker thread if joinable.
     * 
     */
    void
    join_worker_thread();

    /**
     * @brief Starts the event collector.
     * 
     */
    void
    start();

    /**
     * @brief Stops the event collector.
     * 
     */
    void
    stop();
};


#endif // EVENT_COLLECTOR_HPP