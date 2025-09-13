#include "../include/kitchen.hpp"

#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include "filtered_logger.hpp"
#include "discovery_and_connection.hpp"
#include "information_node_reader.hpp"

#define INSTANCE_NAME "CpsKitchen"
#define REMOTE_CONTROLLER_INSTANCE_NAME "RemoteKitchenController"
#define REMOTE_CONVEYOR_INSTANCE_NAME "RemoteKitchenConveyor"

kitchen::kitchen(uint32_t _robot_count) : server_(UA_Server_new()), kitchen_uri_("urn:kitchen:env"), kitchen_type_inserter_(server_, KITCHEN_TYPE), running_(true), remote_robot_type_inserter_(server_, REMOTE_ROBOT_TYPE), robot_count_(_robot_count), remote_controller_type_inserter_(server_, REMOTE_CONTROLLER_TYPE),
                        remote_conveyor_type_inserter_(server_, REMOTE_CONVEYOR_TYPE), mersenne_twister_(random_device_()), uniform_int_distribution_(1,3), controller_client_(nullptr), conveyor_client_(nullptr) {
    /* Setup kitchen environment */
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error with setting up the server", __FUNCTION__);
        return;
    }
    // Set a unique application URI for the robot
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC(kitchen_uri_.c_str());
    // *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add kitchen attributes */
    kitchen_type_inserter_.add_attribute(KITCHEN_TYPE, ASSIGNED_ORDERS);
    kitchen_type_inserter_.add_attribute(KITCHEN_TYPE, DROPPED_ORDERS);
    /* Add place random order method node */
    method_arguments place_random_order_arguments;
    place_random_order_arguments.add_output_argument("indicates whether the robot is instructed", "robot_instructed", UA_TYPES_BOOLEAN);
    status = kitchen_type_inserter_.add_method(KITCHEN_TYPE, PLACE_RANDOM_ORDER, place_random_order, place_random_order_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, PLACE_RANDOM_ORDER);
        return;
    }

    /* Add kitchen type constructor */
    kitchen_type_inserter_.add_object_type_constructor(server_, kitchen_type_inserter_.get_object_type_id(KITCHEN_TYPE));
    /* Instantiate kitchen type */
    kitchen_type_inserter_.add_object_instance(INSTANCE_NAME, KITCHEN_TYPE);
    UA_UInt32 initial_orders_count = 0;
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, ASSIGNED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, DROPPED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, RECEIVED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, COMPLETED_ORDERS, &initial_orders_count, UA_TYPES_UINT32);
    /* Add the remote controller type */
    UA_Boolean initial_connectivity_state = false;
    remote_controller_type_inserter_.add_attribute(REMOTE_CONTROLLER_TYPE, CONNECTIVITY);
    remote_controller_type_inserter_.add_object_type_constructor(server_, remote_controller_type_inserter_.get_object_type_id(REMOTE_CONTROLLER_TYPE));
    remote_controller_type_inserter_.add_object_instance(REMOTE_CONTROLLER_INSTANCE_NAME, REMOTE_CONTROLLER_TYPE, kitchen_type_inserter_.get_instance_id(INSTANCE_NAME), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
    remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &initial_connectivity_state, UA_TYPES_BOOLEAN);
    /* Add the remote conveyor type */
    remote_conveyor_type_inserter_.add_attribute(REMOTE_CONVEYOR_TYPE, CONNECTIVITY);
    remote_conveyor_type_inserter_.add_object_type_constructor(server_, remote_conveyor_type_inserter_.get_object_type_id(REMOTE_CONVEYOR_TYPE));
    remote_conveyor_type_inserter_.add_object_instance(REMOTE_CONVEYOR_INSTANCE_NAME, REMOTE_CONVEYOR_TYPE, kitchen_type_inserter_.get_instance_id(INSTANCE_NAME), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
    remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &initial_connectivity_state, UA_TYPES_BOOLEAN);
    /* Add remote robot type constructor */
    if (remote_robot::setup_remote_robot_object_type(remote_robot_type_inserter_, server_) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the remote robot type constructor", __FUNCTION__);
        stop();
        return;
    }
    /* Add remote robot instances to the address space */
    for (position_t position = 1; position <= robot_count_; position++) {
        status = remote_robot_type_inserter_.add_object_instance(remote_robot::remote_robot_instance_name(position).c_str(), REMOTE_ROBOT_TYPE, kitchen_type_inserter_.get_instance_id(INSTANCE_NAME), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT));
        status |= remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(position), POSITION, &position, UA_TYPES_UINT32);
        status |= remote_robot_type_inserter_.set_scalar_attribute(remote_robot::remote_robot_instance_name(position), CONNECTIVITY, &initial_connectivity_state, UA_TYPES_BOOLEAN);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding remote robot object and setting initial attributes (%s)", __FUNCTION__, UA_StatusCode_name(status));
            stop();
            return;
        }
    }
    /* Run the kitchen server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error at kitchen startup");
        return;
    }
    /* Register at discovery server repeatedly */
    if (discovery_util_.register_server_repeatedly(server_) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Failed to start discovery register", __FUNCTION__);
        stop();
        return;
    }
    /* Start the kitchen event loop */
    try {
        server_iterate_thread_ = std::thread([this]() {
            while(running_) {
                UA_Server_run_iterate(server_, true);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running kitchen");
        running_ = false;
        return;
    }
    /* Setup controller client */
    std::string controller_endpoint;
    while((status = discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to controller, retrying in %d seconds (%s)", __FUNCTION__, LOOKUP_INTERVAL, UA_StatusCode_name(status));
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        if (!running_) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to controller", __FUNCTION__);
            stop();
            return;
        }
    }
    UA_Boolean connectivity_state = true;
    remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
    /* Gather method ids */
    if ((method_id_map_[CHOOSE_NEXT_ROBOT] = node_browser_helper().get_method_id(controller_endpoint, CONTROLLER_TYPE, CHOOSE_NEXT_ROBOT)) == OBJECT_METHOD_INFO_NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s method id", __FUNCTION__, CHOOSE_NEXT_ROBOT);
        stop();
        return;        
    }
    /* Setup conveyor client */
    std::string conveyor_endpoint;
    while((status = discover_and_connect(conveyor_client_, discovery_util_, conveyor_endpoint, CONVEYOR_TYPE)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to conveyor, retrying in %d seconds (%s)", __FUNCTION__, LOOKUP_INTERVAL, UA_StatusCode_name(status));
        std::this_thread::sleep_for(std::chrono::seconds(LOOKUP_INTERVAL));
        if (!running_) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error discovering and connecting to conveyor", __FUNCTION__);
            stop();
            return;
        }
    }
    remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
}

kitchen::~kitchen() {
    stop();
    join_threads();
    UA_Server_run_shutdown(server_);
    UA_Server_delete(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Destructor finished successfully", __FUNCTION__);
}

UA_StatusCode
kitchen::place_random_order(UA_Server* _server,
        const UA_NodeId* _session_id, void* _session_context,
        const UA_NodeId* _method_id, void* _method_context,
        const UA_NodeId* _object_id, void* _object_context,
        size_t _input_size, const UA_Variant* _input,
        size_t _output_size, UA_Variant* _output) {
    if(_input_size != 0) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad input size", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    /* Extract method context */
    if(_method_context == NULL) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Method context is NULL", __FUNCTION__);
        return UA_STATUSCODE_BAD;
    }
    kitchen* self = static_cast<kitchen*>(_method_context);
    self->handle_random_order_request(_output);
    return UA_STATUSCODE_GOOD;
}

void
kitchen::handle_random_order_request(UA_Variant* _output) {
    bool instructed = false;
    recipe_id_t recipe_id = uniform_int_distribution_(mersenne_twister_);
    object_method_info omi = method_id_map_[CHOOSE_NEXT_ROBOT];
    UA_Variant* next_suitable_robot_output;
    size_t next_suitable_robot_output_size;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        method_node_caller choose_next_robot_caller;
        UA_UInt32 processed_steps = 0;
        choose_next_robot_caller.add_scalar_input_argument(&recipe_id, UA_TYPES_UINT32);
        choose_next_robot_caller.add_scalar_input_argument(&processed_steps, UA_TYPES_UINT32);
        UA_StatusCode status = UA_STATUSCODE_UNCERTAIN;
        if ((status = choose_next_robot_caller.call_method_node(controller_client_, omi.object_id_, omi.method_id_, &next_suitable_robot_output_size, &next_suitable_robot_output)) != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error calling choose next robot (%s)", __FUNCTION__, UA_StatusCode_name(status));
            increment_orders_counter(DROPPED_ORDERS);
            UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);
            return;
        }
    }
    remote_robot* next_suitable_robot = nullptr;
    {
        std::lock_guard<std::mutex> lock(remote_robot_discovery_mutex_);
        remove_marked_robots();
        next_suitable_robot = choose_next_robot_called(next_suitable_robot_output_size, next_suitable_robot_output);
    }
    if (next_suitable_robot != nullptr) {
        UA_Variant* output;
        size_t output_size;
        UA_StatusCode status = next_suitable_robot->instruct(recipe_id, 0, &output_size, &output);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Calling instruct on remote robot failed (%s)", __FUNCTION__, UA_StatusCode_name(status));
            instructed = false;
            increment_orders_counter(DROPPED_ORDERS);
            UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);
            return;
        }
        if (instructed = receive_robot_task_called(output_size, output)) {
            increment_orders_counter(ASSIGNED_ORDERS);
        } else {
            increment_orders_counter(DROPPED_ORDERS);
        }
    }
    UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);
}

bool
kitchen::receive_robot_task_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return false;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_UINT32])
       || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return false;
    }

    position_t remote_robot_position = *(position_t*) _output[0].data;
    UA_Boolean result = *(UA_Boolean*) _output[1].data;

    remote_robot* robot = position_remote_robot_map_[remote_robot_position].get();
    // Sanity check
    if(robot->get_position() != remote_robot_position) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Mismatch on position. Received position %d, actually %d", __FUNCTION__, remote_robot_position, robot->get_position());
        return false;
    }
    if (!result) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d returned false", __FUNCTION__, robot->get_position());
        return false;
    }
    return true;
}

remote_robot*
kitchen::choose_next_robot_called(size_t _output_size, UA_Variant *_output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        stop();
        return nullptr;
    }
    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_STRING])
    || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_UINT32])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return nullptr;
    }
    UA_String remote_robot_endpoint = *(UA_String*) _output[0].data;
    UA_UInt32 remote_robot_position = *(UA_UInt32*) _output[1].data;
    std::string remote_robot_endpoint_str((char*) remote_robot_endpoint.data, remote_robot_endpoint.length);
    if (position_remote_robot_map_.find(remote_robot_position) == position_remote_robot_map_.end())
        position_remote_robot_map_[remote_robot_position] = std::make_unique<remote_robot>(remote_robot_endpoint_str, remote_robot_position, remote_robot_type_inserter_, std::bind(&kitchen::mark_robot_for_removal, this, std::placeholders::_1));
    if (robots_to_be_removed_.find(remote_robot_position) != robots_to_be_removed_.end()) {
        remove_marked_robots();
        remote_robot_discovery_cv.notify_all();
        return nullptr;
    }
    return position_remote_robot_map_[remote_robot_position].get();
}

UA_StatusCode
kitchen::increment_orders_counter(std::string _attribute_name) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_Variant value;
    if ((status = kitchen_type_inserter_.get_attribute(INSTANCE_NAME, _attribute_name.c_str(), value)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error getting attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
        return status;
    }
    UA_UInt32 dishes_counter = *(UA_UInt32*) value.data;
    dishes_counter++;
    if ((status = kitchen_type_inserter_.set_scalar_attribute(INSTANCE_NAME, _attribute_name.c_str(), &dishes_counter, UA_TYPES_UINT32)) != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error setting attribute (%s)", __FUNCTION__, UA_StatusCode_name(status));
        return status;
    }
    return status;
}

void
kitchen::mark_robot_for_removal(position_t _position) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    std::lock_guard<std::mutex> lock(mark_for_removal_mutex_);
    robots_to_be_removed_.insert(_position);
}

void
kitchen::remove_marked_robots() {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    for (position_t position : robots_to_be_removed_) {
        if (position_remote_robot_map_.find(position) != position_remote_robot_map_.end()) {
            position_remote_robot_map_.erase(position);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Removed remote robot at position %d", position);
        } else {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "No remote robot found at position %d", position);
        }
    }
    robots_to_be_removed_.clear();
}

void
kitchen::join_threads() {
    if (server_iterate_thread_.joinable())
        server_iterate_thread_.join();
    if (client_iterate_thread_.joinable())
        client_iterate_thread_.join();
    if (cyclic_remote_robot_discovery_thread_.joinable())
        cyclic_remote_robot_discovery_thread_.join();
}

void
kitchen::start() {
    /* Run the client iterate thread */
    try {
        client_iterate_thread_ = std::thread([this]() {
            while(running_) {
                {
                    std::lock_guard<std::mutex> lock(client_mutex_);
                    if (controller_client_ != nullptr) {
                        UA_StatusCode status = UA_Client_run_iterate(controller_client_, 1);
                        if (status != UA_STATUSCODE_GOOD) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running controller client iterate", __FUNCTION__);
                            UA_Client_delete(controller_client_);
                            controller_client_ = nullptr;
                            UA_Boolean connectivity_state = false;
                            remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                        }
                    } else {
                        std::string controller_endpoint;
                        if (discover_and_connect(controller_client_, discovery_util_, controller_endpoint, CONTROLLER_TYPE) != UA_STATUSCODE_GOOD)
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error reconnecting to controller. Retrying ...", __FUNCTION__);
                        else {
                            UA_Boolean connectivity_state = true;
                            remote_controller_type_inserter_.set_scalar_attribute(REMOTE_CONTROLLER_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                        }

                    }

                    if (conveyor_client_ != nullptr) {
                        UA_StatusCode status = UA_Client_run_iterate(conveyor_client_, 1);
                        if (status != UA_STATUSCODE_GOOD) {
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error running conveyor client iterate", __FUNCTION__);
                            UA_Client_delete(conveyor_client_);
                            conveyor_client_ = nullptr;
                            UA_Boolean connectivity_state = false;
                            remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                        }
                    } else {
                        std::string conveyor_endpoint;
                        if (discover_and_connect(conveyor_client_, discovery_util_, conveyor_endpoint, CONVEYOR_TYPE) != UA_STATUSCODE_GOOD)
                            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error reconnecting to conveyor. Retrying ...", __FUNCTION__);
                        else {
                            UA_Boolean connectivity_state = true;
                            remote_conveyor_type_inserter_.set_scalar_attribute(REMOTE_CONVEYOR_INSTANCE_NAME, CONNECTIVITY, &connectivity_state, UA_TYPES_BOOLEAN);
                        }
                    }
                }
                if (remote_robot_discovery_mutex_.try_lock()) {
                    if (position_remote_robot_map_.size() < robot_count_)
                        remote_robot_discovery_cv.notify_all();
                    remote_robot_discovery_mutex_.unlock();
                }
                if (usleep(1*1000)) {
                    UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error at client iterate sleep", __FUNCTION__);
                    stop();
                    return;
                }
                // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Starting the next client iterate", __FUNCTION__);
            }
        });
    } catch (...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the client iterate thread");
        stop();
        return;
    }
    /* Run the cyclic remote robot discovery thread */
    try {
        cyclic_remote_robot_discovery_thread_ = std::thread([this]() {
            while (running_) {
                {
                    std::unique_lock<std::mutex> lock(remote_robot_discovery_mutex_);
                    std::vector<std::string> endpoints;
                    if (discovery_util_.lookup_endpoints(endpoints) != UA_STATUSCODE_GOOD) {
                        continue;
                    }
                    for (std::string endpoint : endpoints) {
                        if (node_browser_helper().has_instance(endpoint, ROBOT_TYPE)) {
                            /* Get position remote robot's position */
                            UA_Client* remote_robot_client = nullptr;
                            client_connection_establisher cce;
                            bool connected = cce.establish_connection(remote_robot_client, endpoint);
                            if (!connected) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error establishing robot client session", __FUNCTION__);
                                UA_Client_delete(remote_robot_client);
                                continue;
                            }
                            UA_NodeId position_node_id = node_browser_helper().get_attribute_id(remote_robot_client, ROBOT_TYPE, POSITION);
                            if (UA_NodeId_equal(&position_node_id, &UA_NODEID_NULL)) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not find the %s attribute id", __FUNCTION__, POSITION);
                                continue;
                            }
                            information_node_reader inr;
                            if (inr.read_information_node(remote_robot_client, position_node_id) != UA_STATUSCODE_GOOD) {
                                UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Could not read the %s attribute id", __FUNCTION__, POSITION);
                                continue;
                            }
                            position_t remote_robot_position = *(position_t*)inr.get_variant()->data;
                            if (position_remote_robot_map_.find(remote_robot_position) == position_remote_robot_map_.end()) {
                                position_remote_robot_map_[remote_robot_position] = std::make_unique<remote_robot>(endpoint, remote_robot_position, remote_robot_type_inserter_, std::bind(&kitchen::mark_robot_for_removal, this, std::placeholders::_1));
                            }
                            if (robots_to_be_removed_.find(remote_robot_position) != robots_to_be_removed_.end()){
                                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Instantiating remote robot at position %d failed", __FUNCTION__, remote_robot_position);
                                remove_marked_robots();
                            }
                        }
                    }
                    if (position_remote_robot_map_.size() == robot_count_)
                        remote_robot_discovery_cv.wait(lock);
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }
    catch(...) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error running the remote robot discovery thread");
        stop();
        return;
    }
    join_threads();
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Exited start method", __FUNCTION__);
}

void
kitchen::stop() {
    {
        std::lock_guard<std::mutex> lock(remote_robot_discovery_mutex_);
        running_ = false;
        remote_robot_discovery_cv.notify_all();
    }
    discovery_util_.stop();
    discovery_util_.deregister_server(server_);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Stop finished successfully", __FUNCTION__);
}