#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <signal.h>
#include <string>
#include <iostream>

#include "client_connection_establisher.hpp"
#include "information_node_reader.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}


UA_NodeId browse_object_type(UA_Client* _client, UA_NodeId _start_node_id, std::string _target_browsename) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = _start_node_id;
    bd.referenceTypeId = UA_NS0ID(HASSUBTYPE);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_OBJECTTYPE;
    bd.resultMask = UA_BROWSERESULTMASK_BROWSENAME;

    UA_BrowseResult bres = UA_Client_browse(_client, NULL, 0, &bd);
    UA_NodeId matching_node_id = UA_NODEID_NULL;
    for(size_t i = 0; i < bres.referencesSize; ++i) {
        const UA_ReferenceDescription *ref = &(bres.references[i]);
        std::string browse_name((char*) ref->browseName.name.data, ref->browseName.name.length);
        if (!browse_name.compare(_target_browsename))
            matching_node_id = ref->nodeId.nodeId;
    }

    UA_BrowseResult_clear(&bres);
    return matching_node_id;
}

void browse_objects(UA_Client* _client, UA_BrowseResult& _browse_result) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = UA_NS0ID(OBJECTSFOLDER);
    bd.referenceTypeId = UA_NS0ID(ORGANIZES);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_OBJECT;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;
    _browse_result = UA_Client_browse(_client, NULL, 0, &bd);
}

UA_NodeId browse_instance(UA_Client* _client, UA_NodeId _start_node_id, UA_NodeId _target_type_node_id) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = _start_node_id;
    bd.referenceTypeId = UA_NS0ID(HASTYPEDEFINITION);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;

    UA_BrowseResult bres = UA_Client_browse(_client, NULL, 0, &bd);
    UA_NodeId matching_node_id = UA_NODEID_NULL;
    if (bres.referencesSize > 0) {
        UA_NodeId* type_node_id = &bres.references[0].nodeId.nodeId;
        if (UA_NodeId_equal(type_node_id, &_target_type_node_id)) {
            matching_node_id = _start_node_id;
        }
    }

    UA_BrowseResult_clear(&bres);
    return matching_node_id;
}


int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_Client* client = UA_Client_new();
    client_connection_establisher con_estab;
    UA_SessionState session_state = con_estab.establish_connection(client, 5000);
    if (session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing client session", __FUNCTION__);
        running = false;
        return UA_STATUSCODE_BAD;
    }

    UA_NodeId node_id = browse_object_type(client, UA_NS0ID(BASEOBJECTTYPE), "ControllerType");
    std::cout << "ControllerType Node id(" << node_id.namespaceIndex << "," << node_id.identifier.numeric << ")" << std::endl;

    UA_BrowseResult browse_result;
    browse_objects(client, browse_result);

    for (size_t i = 0; i < browse_result.referencesSize; i++) {
        const UA_ReferenceDescription* reference_description = &browse_result.references[i];
        UA_NodeId instance_node_id = browse_instance(client, reference_description->nodeId.nodeId, node_id);
        if (!UA_NodeId_equal(&instance_node_id, &UA_NODEID_NULL)) {
            UA_QualifiedName instance_browse_name;
            UA_Client_readBrowseNameAttribute(client, instance_node_id, &instance_browse_name);
            std::string bm((char*) instance_browse_name.name.data, instance_browse_name.name.length);
            std::cout << bm << " Node id(" << instance_node_id.namespaceIndex << "," << instance_node_id.identifier.numeric << ")" << std::endl;
        }
    }

    UA_BrowseResult_clear(&browse_result);
    UA_Client_delete(client);
    return 0;
}