#include "../include/object_type_node_inserter.hpp"
#include <open62541/plugin/log_stdout.h>

object_type_node_inserter::object_type_node_inserter(UA_Server* _server, const char* _parent_object_type_name) : server_(_server) {
    UA_NodeId parent_object_type_id;
    UA_StatusCode status;
    UA_ObjectTypeAttributes attribute = UA_ObjectTypeAttributes_default;
    attribute.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(_parent_object_type_name));
    status = UA_Server_addObjectTypeNode(server_, UA_NODEID_NULL,
                                UA_NS0ID(BASEOBJECTTYPE), UA_NS0ID(HASSUBTYPE),
                                UA_QUALIFIEDNAME(1, const_cast<char*>(_parent_object_type_name)), attribute,
                                NULL, &parent_object_type_id);
    if (status != UA_STATUSCODE_GOOD)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Adding object type node %s failed", _parent_object_type_name);
    parent_object_type_id_ = parent_object_type_id;
    object_type_ids_[std::string(_parent_object_type_name)] = parent_object_type_id_;
}

object_type_node_inserter::~object_type_node_inserter() {
}

void
object_type_node_inserter::add_attribute(std::string _parent_object_type_name, const char* _attribute_name, bool _mandatory) {
    if (!has_object_type(_parent_object_type_name)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Unknown object type. Attribute is not added");
        return;
    }
    UA_VariableAttributes attribute = UA_VariableAttributes_default;
    attribute.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(_attribute_name));
    UA_NodeId attribute_id;
    UA_StatusCode status = UA_Server_addVariableNode(server_, UA_NODEID_NULL, object_type_ids_[_parent_object_type_name],
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
object_type_node_inserter::add_object_sub_type(const char* _object_type_name) {
    UA_NodeId object_type_id;
    UA_ObjectTypeAttributes object_type_attribute = UA_ObjectTypeAttributes_default;
    object_type_attribute.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(_object_type_name));
    UA_Server_addObjectTypeNode(server_, UA_NODEID_NULL, parent_object_type_id_, UA_NS0ID(HASSUBTYPE),
                                UA_QUALIFIEDNAME(1, const_cast<char*>(_object_type_name)), object_type_attribute,
                                NULL, &object_type_id);
    object_type_ids_[std::string(_object_type_name)] = object_type_id;
}

void
object_type_node_inserter::make_mandatory(UA_NodeId _attribute_id) {
    UA_StatusCode status = UA_Server_addReference(server_, _attribute_id, UA_NS0ID(HASMODELLINGRULE),
                           UA_NS0EXID(MODELLINGRULE_MANDATORY), true);
    if (status != UA_STATUSCODE_GOOD)
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Making attribute node mandatory failed");
}

void
object_type_node_inserter::add_object_instance(const char* _instance_name, const char* _object_type_name) {
    if (!has_object_type(std::string(_object_type_name))) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Unknown type name. Instance is not added");
        return;
    }
    UA_NodeId node_id;
    UA_ObjectAttributes object_attribute = UA_ObjectAttributes_default;
    object_attribute.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(_instance_name));
    UA_Server_addObjectNode(server_, UA_NODEID_NULL,
                            UA_NS0ID(OBJECTSFOLDER), UA_NS0ID(ORGANIZES),
                            UA_QUALIFIEDNAME(1, const_cast<char*>(_instance_name)),
                            object_type_ids_[std::string(_object_type_name)],
                            object_attribute, NULL, &node_id);
    instance_ids_[std::string(_instance_name)] = node_id;
}

UA_StatusCode
object_type_node_inserter::object_type_constructor(UA_Server* _server,
                        const UA_NodeId* _session_id, void* _session_context,
                        const UA_NodeId* _type_id, void* _type_context,
                        const UA_NodeId* _node_id, void** _node_context) {
    // Get object type
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.referenceTypeId = UA_NS0ID(HASTYPEDEFINITION);
    bd.resultMask = UA_BROWSERESULTMASK_DISPLAYNAME;
    bd.nodeId = *_node_id;
    bd.nodeClassMask = UA_NODECLASS_OBJECTTYPE;
    UA_BrowseResult br = UA_Server_browse(_server, 1, &bd);
    if (br.statusCode != UA_STATUSCODE_GOOD)
        return UA_STATUSCODE_BAD;
    std::string type_display_name((char*) br.references->displayName.text.data, br.references->displayName.text.length);
    UA_BrowseResult_clear(&br);
    // Get instance name
    UA_LocalizedText localized_text;
    UA_LocalizedText_init(&localized_text);
    UA_StatusCode status = UA_Server_readDisplayName(_server, *_node_id, &localized_text);
    if (status != UA_STATUSCODE_GOOD) {
        return UA_STATUSCODE_BAD;
    }
    std::string instance_display_name((char*) localized_text.text.data, localized_text.text.length);
    UA_LocalizedText_clear(&localized_text);

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s instance of type %s created ", instance_display_name.c_str(), type_display_name.c_str());
    return UA_STATUSCODE_GOOD;
}

void
object_type_node_inserter::add_object_type_constructor(UA_Server* _server, UA_NodeId _object_type_id) {
    UA_NodeTypeLifecycle lifecycle;
    lifecycle.constructor = object_type_constructor;
    lifecycle.destructor = NULL;
    UA_Server_setNodeTypeLifecycle(_server, _object_type_id, lifecycle);
}

UA_NodeId
object_type_node_inserter::get_object_type_id(std::string _object_type_name) {
    UA_NodeId object_type_id = UA_NODEID_NUMERIC(0, 0);
    if (has_object_type(_object_type_name)) {
        object_type_id = object_type_ids_[_object_type_name];
    }
    return object_type_id;
}

bool
object_type_node_inserter::has_object_type(std::string _object_type_name) {
    return object_type_ids_.find(_object_type_name) != object_type_ids_.end();
}

bool
object_type_node_inserter::has_instance(std::string _instance_name) {
    return instance_ids_.find(_instance_name) != instance_ids_.end();
}

UA_StatusCode
object_type_node_inserter::find_attribute_node_id(std::string _instance_name, const char* _attribute_name, UA_NodeId& _node_id) {
    if (!has_instance(_instance_name)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Unknown instance. Attribute can not be found");
        return UA_STATUSCODE_BAD;
    }

    UA_RelativePathElement rpe;
    UA_RelativePathElement_init(&rpe);
    rpe.referenceTypeId = UA_NS0ID(HASCOMPONENT);
    rpe.isInverse = false;
    rpe.includeSubtypes = false;
    rpe.targetName = UA_QUALIFIEDNAME(1, const_cast<char*>(_attribute_name));

    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = instance_ids_[_instance_name];
    bp.relativePath.elementsSize = 1;
    bp.relativePath.elements = &rpe;

    UA_BrowsePathResult bpr = UA_Server_translateBrowsePathToNodeIds(server_, &bp);
    if(bpr.statusCode != UA_STATUSCODE_GOOD || bpr.targetsSize < 1) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Failed to find attribute %s for instance %s", _attribute_name, _instance_name.c_str());
        return UA_STATUSCODE_BAD;
    }
    _node_id = bpr.targets[0].targetId.nodeId;
    UA_BrowsePathResult_clear(&bpr); // NOTE: may be a problem if node identifier is not numeric
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode
object_type_node_inserter::set_scalar_attribute(std::string _instance_name, const char* _attribute_name, void* _value, UA_UInt32 _type_index) {
    UA_Variant value;
    UA_Variant_setScalar(&value, _value, &UA_TYPES[_type_index]);
    return set_attribute(_instance_name, _attribute_name, value);
}

UA_StatusCode
object_type_node_inserter::set_array_attribute(std::string _instance_name, const char* _attribute_name, void* _array, size_t _array_size, UA_UInt32 _type_index) {
    UA_Variant value;
    UA_Variant_setArray(&value, _array, _array_size, &UA_TYPES[_type_index]);
    return set_attribute(_instance_name, _attribute_name, value);
}

UA_StatusCode
object_type_node_inserter::set_attribute(std::string _instance_name, const char* _attribute_name, UA_Variant& _value) {
    UA_NodeId attribute_node_id;
    UA_StatusCode status_code = find_attribute_node_id(_instance_name, _attribute_name, attribute_node_id);
    if (status_code != UA_STATUSCODE_GOOD) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Could not set the attribute %s for instance %s", _attribute_name, _instance_name.c_str());
        return status_code;
    }

    UA_Server_writeValue(server_, attribute_node_id, _value);
    return UA_STATUSCODE_GOOD;
}