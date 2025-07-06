#include "../include/node_browser.hpp"
#include "../include/client_browse.h"

node_browser::node_browser(/* args */) {
}

node_browser::~node_browser() {
}

UA_NodeId
node_browser::browse_object_type(UA_Client* _client, UA_NodeId _start_node_id, std::string _object_type_name) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = _start_node_id;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_OBJECTTYPE;
    bd.resultMask = UA_BROWSERESULTMASK_BROWSENAME;

    UA_BrowseResult bres = UA_Client_browse(_client, NULL, 0, &bd);
    UA_NodeId matching_node_id = UA_NODEID_NULL;
    for(size_t i = 0; i < bres.referencesSize; ++i) {
        const UA_ReferenceDescription *ref = &(bres.references[i]);
        std::string browse_name((char*) ref->browseName.name.data, ref->browseName.name.length);
        if (!browse_name.compare(_object_type_name)) {
            matching_node_id = ref->nodeId.nodeId;
            break;
        }
    }

    UA_BrowseResult_clear(&bres);
    return matching_node_id;
}

void
node_browser::browse_objects(UA_Client* _client, UA_BrowseResult& _browse_result) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_OBJECT;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;
    _browse_result = UA_Client_browse(_client, NULL, 0, &bd);
}

void
node_browser::browse_methods(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = _instance_id;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_METHOD;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;
    _browse_result = UA_Client_browse(_client, NULL, 0, &bd);
}

void
node_browser::browse_objects(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = _instance_id;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_OBJECT;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;
    _browse_result = UA_Client_browse(_client, NULL, 0, &bd);
}

void
node_browser::browse_attributes(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = _instance_id;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
    bd.browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bd.includeSubtypes = true;
    bd.nodeClassMask = UA_NODECLASS_VARIABLE;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;
    _browse_result = UA_Client_browse(_client, NULL, 0, &bd);
}