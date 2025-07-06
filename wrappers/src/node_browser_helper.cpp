#include "../include/node_browser_helper.hpp"
#include "../include/node_browser.hpp"
#include "../include/client_connection_establisher.hpp"

node_browser_helper::node_browser_helper() {
}

node_browser_helper::~node_browser_helper() {
}

object_method_info
node_browser_helper::get_method_id(UA_Client* _client, std::string _object_type_name, std::string _method_name) {
    object_method_info omi;
    node_browser nb;
    UA_NodeId object_type_id = nb.browse_object_type(_client, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), _object_type_name);
    if (UA_NodeId_equal(&object_type_id, &UA_NODEID_NULL)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: There is no object type with the browse name %s", __FUNCTION__, _object_type_name.c_str());
        return omi;
    }
    UA_BrowseResult browse_objects_result;
    nb.browse_objects(_client, browse_objects_result);
    /* Browse methods and find instance with object type definition */
    for (size_t i = 0; i < browse_objects_result.referencesSize; i++) {
        const UA_ReferenceDescription* object_reference_description = &browse_objects_result.references[i];
        if (UA_NodeId_equal(&object_reference_description->typeDefinition.nodeId, &object_type_id)) {
            /* Browse methods of found instance */
            UA_BrowseResult browse_methods_result;
            nb.browse_methods(_client, object_reference_description->nodeId.nodeId, browse_methods_result);
            for (size_t i = 0; i < browse_methods_result.referencesSize; i++) {
                const UA_ReferenceDescription* method_reference_description = &browse_methods_result.references[i];
                UA_QualifiedName method_browse_name = method_reference_description->browseName;
                std::string method_name((char*) method_browse_name.name.data, method_browse_name.name.length);
                if (!method_name.compare(_method_name)) {
                    omi.object_id_ = object_reference_description->nodeId.nodeId;
                    omi.method_id_ = method_reference_description->nodeId.nodeId;
                    break;
                }
            }
            UA_BrowseResult_clear(&browse_methods_result);
            break;
        }
    }
    UA_BrowseResult_clear(&browse_objects_result);
    return omi;
}

UA_NodeId
node_browser_helper::get_attribute_id(UA_Client* _client, std::string _object_type_name, std::string _attribute_name) {
    UA_NodeId attribute_id = UA_NODEID_NULL;
    node_browser nb;
    UA_NodeId object_type_id = nb.browse_object_type(_client, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), _object_type_name);
    if (UA_NodeId_equal(&object_type_id, &UA_NODEID_NULL)) {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%s: There is no object type with the browse name %s", __FUNCTION__, _object_type_name.c_str());
        return attribute_id;
    }
    UA_BrowseResult browse_objects_result;
    nb.browse_objects(_client, browse_objects_result);
    /* Browse attributes and find instance with object type definition */
    for (size_t i = 0; i < browse_objects_result.referencesSize; i++) {
        const UA_ReferenceDescription* object_reference_description = &browse_objects_result.references[i];
        if (UA_NodeId_equal(&object_reference_description->typeDefinition.nodeId, &object_type_id)) {
            /* Browse attributes of found instance */
            UA_BrowseResult browse_attributes_result;
            nb.browse_attributes(_client, object_reference_description->nodeId.nodeId, browse_attributes_result);
            for (size_t i = 0; i < browse_attributes_result.referencesSize; i++) {
                const UA_ReferenceDescription* attribute_reference_description = &browse_attributes_result.references[i];
                UA_QualifiedName attribute_browse_name = attribute_reference_description->browseName;
                std::string attribute_name((char*) attribute_browse_name.name.data, attribute_browse_name.name.length);
                if (!attribute_name.compare(_attribute_name)) {
                    attribute_id = attribute_reference_description->nodeId.nodeId;
                    break;
                }
            }
            UA_BrowseResult_clear(&browse_attributes_result);
            break;
        }
    }
    UA_BrowseResult_clear(&browse_objects_result);
    return attribute_id;
}

object_method_info
node_browser_helper::get_method_id(std::string _server_endpoint, std::string _object_type_name, std::string _method_name) {
    UA_Client* client = UA_Client_new();
    client_connection_establisher cce(client);
    bool connected = cce.establish_connection(_server_endpoint);
    if (!connected) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing client session to endpoint %s", __FUNCTION__, _server_endpoint.c_str());
        return OBJECT_METHOD_INFO_NULL;
    }
    object_method_info omi = get_method_id(client, _object_type_name, _method_name);
    UA_Client_delete(client);
    return omi;
}


UA_NodeId
node_browser_helper::get_attribute_id(std::string _server_endpoint, std::string _object_type_name, std::string _attribute_name) {
    UA_Client* client = UA_Client_new();
    client_connection_establisher cce(client);
    bool connected = cce.establish_connection(_server_endpoint);
    if (!connected) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing client session to endpoint %s", __FUNCTION__, _server_endpoint.c_str());
        return UA_NODEID_NULL;
    }
    UA_NodeId node_id = get_attribute_id(client, _object_type_name, _attribute_name);
    UA_Client_delete(client);
    return node_id;
}