#include "../include/object_type_node_inserter.hpp"
#include <open62541/plugin/log_stdout.h>

object_type_node_inserter::object_type_node_inserter(UA_Server* _server, const char* _parent_object_name) : server_(_server) {
    UA_NodeId parent_object_id;
    UA_StatusCode status;
    UA_ObjectTypeAttributes attribute = UA_ObjectTypeAttributes_default;
    attribute.displayName = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_parent_object_name));
    status = UA_Server_addObjectTypeNode(server_, UA_NODEID_NULL,
                                UA_NS0ID(BASEOBJECTTYPE), UA_NS0ID(HASSUBTYPE),
                                UA_QUALIFIEDNAME(1, const_cast<char*>(_parent_object_name)), attribute,
                                NULL, &parent_object_id);
    if (status != UA_STATUSCODE_GOOD)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Adding object type node %s failed", _parent_object_name);
    parent_object_id_ = parent_object_id;
    object_ids_[std::string(_parent_object_name)] = parent_object_id_;
}

object_type_node_inserter::~object_type_node_inserter() {
}

void
object_type_node_inserter::add_attribute(std::string _parent_object_name, const char* _attribute_name, bool _mandatory) {
    if (!has_object(_parent_object_name)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Unknown parent object. Attribute is not added");
        return;
    }
    UA_VariableAttributes attribute = UA_VariableAttributes_default;
    attribute.displayName = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_attribute_name));
    UA_NodeId attribute_id;
    UA_StatusCode status = UA_Server_addVariableNode(server_, UA_NODEID_NULL, object_ids_[_parent_object_name],
                            UA_NS0ID(HASCOMPONENT),
                            UA_QUALIFIEDNAME(1, const_cast<char*>(_attribute_name)),
                            UA_NS0ID(BASEDATAVARIABLETYPE),
                            attribute, NULL, &attribute_id);
    if (status != UA_STATUSCODE_GOOD)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Adding attribute node %s failed", _attribute_name);
    if (_mandatory)
        make_mandatory(attribute_id);
}

void
object_type_node_inserter::add_object_type_attribute(const char* _object_name) {
    UA_NodeId object_id;
    UA_ObjectTypeAttributes object_attribute = UA_ObjectTypeAttributes_default;
    object_attribute.displayName = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_object_name));
    UA_Server_addObjectTypeNode(server_, UA_NODEID_NULL, parent_object_id_, UA_NS0ID(HASSUBTYPE),
                                UA_QUALIFIEDNAME(1, const_cast<char*>(_object_name)), object_attribute,
                                NULL, &object_id);
    object_ids_[std::string(_object_name)] = object_id;
}

void
object_type_node_inserter::make_mandatory(UA_NodeId _attribute_id) {
    UA_StatusCode status = UA_Server_addReference(server_, _attribute_id, UA_NS0ID(HASMODELLINGRULE),
                           UA_NS0EXID(MODELLINGRULE_MANDATORY), true);
    if (status != UA_STATUSCODE_GOOD)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Making attribute node mandatory failed");
}

void
object_type_node_inserter::add_object_instance(const char* _instance_name, const char* _type_name) {
    if (!has_object(std::string(_type_name))) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Unknown type name. Instance is not added");
        return;
    }
    UA_ObjectAttributes object_attribute = UA_ObjectAttributes_default;
    object_attribute.displayName = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_instance_name));
    UA_Server_addObjectNode(server_, UA_NODEID_NULL,
                            UA_NS0ID(OBJECTSFOLDER), UA_NS0ID(ORGANIZES),
                            UA_QUALIFIEDNAME(1, const_cast<char*>(_instance_name)),
                            object_ids_[std::string(_type_name)],
                            object_attribute, NULL, NULL);
}

static UA_StatusCode
object_type_constructor(UA_Server* _server,
                        const UA_NodeId* _session_id, void* _session_context,
                        const UA_NodeId* _type_id, void* _type_context,
                        const UA_NodeId* _node_id, void** _node_context) {
    // TODO: get display name to get instance name and do log
    return UA_STATUSCODE_GOOD;
}

void
object_type_node_inserter::add_object_type_constructor(UA_Server* _server, UA_NodeId _object_id) {
    UA_NodeTypeLifecycle lifecycle;
    lifecycle.constructor = object_type_constructor;
    lifecycle.destructor = NULL;
    UA_Server_setNodeTypeLifecycle(_server, _object_id, lifecycle);
}

UA_NodeId
object_type_node_inserter::get_object_id(std::string _object_name) {
    UA_NodeId object_id = UA_NODEID_NUMERIC(0, 0);
    if (has_object(_object_name)) {
        object_id = object_ids_[_object_name];
    }
    return object_id;
}

bool
object_type_node_inserter::has_object(std::string _object_name) {
    return object_ids_.find(_object_name) != object_ids_.end();
}