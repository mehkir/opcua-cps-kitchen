#include "../include/kitchen.hpp"

#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include "filtered_logger.hpp"
#include "discovery_and_connection.hpp"

#define INSTANCE_NAME "CpsKitchen"

kitchen::kitchen() : server_(UA_Server_new()), kitchen_uri_("urn:kitchen:env"), kitchen_type_inserter_(server_, KITCHEN_TYPE), running_(true), mersenne_twister_(random_device_()), uniform_int_distribution_(1,3) {
    /* Setup kitchen environment */
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    UA_ServerConfig* server_config = UA_Server_getConfig(server_);
    status = UA_ServerConfig_setMinimal(server_config, 0, NULL);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error with setting up the server", __FUNCTION__);
        running_ = false;
        return;
    }
    // Set a unique application URI for the robot
    UA_String_clear(&server_config->applicationDescription.applicationUri);
    server_config->applicationDescription.applicationUri = UA_STRING_ALLOC(kitchen_uri_.c_str());
    // *server_config->logging = filtered_logger().create_filtered_logger(UA_LOGLEVEL_INFO, UA_LOGCATEGORY_USERLAND);
    /* Add place random order method node */
    method_arguments place_random_order_arguments;
    place_random_order_arguments.add_output_argument("indicates whether the robot is instructed", "robot_instructed", UA_TYPES_BOOLEAN);
    status = kitchen_type_inserter_.add_method(KITCHEN_TYPE, PLACE_RANDOM_ORDER, place_random_order, place_random_order_arguments, this);
    if(status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Error adding the %s method node", __FUNCTION__, PLACE_RANDOM_ORDER);
        running_ = false;
        return;
    }
    /* Add kitchen type constructor */
    kitchen_type_inserter_.add_object_type_constructor(server_, kitchen_type_inserter_.get_object_type_id(KITCHEN_TYPE));
    /* Instantiate kitchen type */
    kitchen_type_inserter_.add_object_instance(INSTANCE_NAME, KITCHEN_TYPE);
    /* Run the kitchen server */
    status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Error at kitchen startup");
        running_ = false;
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
}

kitchen::~kitchen() {
    // TODO:
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
    remote_robot* next_suitable_robot = find_suitable_robot(recipe_id, 0);
    if (next_suitable_robot != NULL) {
        UA_Variant* output;
        size_t output_size;
        UA_StatusCode status = next_suitable_robot->instruct(recipe_id, 0, &output_size, &output);
        if (status != UA_STATUSCODE_GOOD) {
            UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Calling instruct on remote robot failed", __FUNCTION__);
            instructed = false;
            UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);
            return;
        }
        receive_robot_task_called(output_size, output);
        instructed = true;
    }
    UA_Variant_setScalarCopy(_output, &instructed, &UA_TYPES[UA_TYPES_BOOLEAN]);
}

void
kitchen::receive_robot_task_called(size_t _output_size, UA_Variant* _output) {
    // UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s called", __FUNCTION__);
    if(_output_size != 2) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output size", __FUNCTION__);
        return;
    }

    if(!UA_Variant_hasScalarType(&_output[0], &UA_TYPES[UA_TYPES_UINT32])
       || !UA_Variant_hasScalarType(&_output[1], &UA_TYPES[UA_TYPES_BOOLEAN])) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Bad output argument type", __FUNCTION__);
        return;
    }


    position_t remote_robot_position = *(position_t*) _output[0].data;
    UA_Boolean result = *(UA_Boolean*) _output[1].data;

    remote_robot* robot = position_remote_robot_map_[remote_robot_position].get();
    // Sanity check
    if(robot->get_position() != remote_robot_position) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Mismatch on position. Received position %d, actually %d", __FUNCTION__, remote_robot_position, robot->get_position());
    }
    if (!result)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: Robot at position %d returned false", __FUNCTION__, robot->get_position());
}

void kitchen::start() {

}

void kitchen::stop() {
    
}