#include "../include/node_browser_helper.hpp"
#include "../include/node_browser.hpp"

node_browser_helper::node_browser_helper() {
}

node_browser_helper::~node_browser_helper() {
}

object_method_info
node_browser_helper::get_method_id(UA_Client* _client, std::string _object_type_name, std::string _method_name) {
    object_method_info omi;
    node_browser nb;
    UA_NodeId object_type_id = nb.browse_object_type(_client, UA_NS0ID(BASEOBJECTTYPE), _object_type_name);
    if (!UA_NodeId_equal(&object_type_id, &UA_NODEID_NULL)) {
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