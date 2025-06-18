#include <stdio.h>
#include <open62541/client_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <signal.h>
#include <string>
#include <vector>
#include <iostream>

#include "client_connection_establisher.hpp"
#include "information_node_reader.hpp"

static volatile UA_Boolean running = true;
static void stopHandler(int sig) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "received ctrl-c");
    running = false;
}


std::vector<UA_NodeId> browse(UA_Client* _client, UA_NodeId _start_node_id, uint32_t _target_namespace = 0, std::string _target_browsename = "") {
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].nodeId = _start_node_id;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
    bReq.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
    bReq.nodesToBrowse[0].referenceTypeId = UA_NODEID_NULL;
    bReq.nodesToBrowse[0].includeSubtypes = true;

    UA_BrowseResponse bResp = UA_Client_Service_browse(_client, bReq);

    std::vector<UA_NodeId> matching_node_ids;
    /* Iterate results */
    for(size_t i = 0; i < bResp.resultsSize; ++i) {
        for(size_t j = 0; j < bResp.results[i].referencesSize; ++j) {
            UA_ReferenceDescription *ref = &(bResp.results[i].references[j]);

            UA_NodeId organizesId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId hasComponentId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
            UA_NodeId hasPropertyId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);

            bool is_allowed_ref_type =
                UA_NodeId_equal(&ref->referenceTypeId, &organizesId) ||
                UA_NodeId_equal(&ref->referenceTypeId, &hasComponentId) ||
                UA_NodeId_equal(&ref->referenceTypeId, &hasPropertyId);

            if (!is_allowed_ref_type)
                continue;

            printf("Node BrowseName: %.*s\n",
               (int)ref->browseName.name.length, ref->browseName.name.data);
            printf("Node DisplayName: %.*s\n",
               (int)ref->displayName.text.length, ref->displayName.text.data);
            
            if (_target_browsename.empty()) {
                matching_node_ids.push_back(ref->nodeId.nodeId);
                continue;
            }
            std::string browsename((char*) ref->browseName.name.data, ref->browseName.name.length);
            if (ref->browseName.namespaceIndex == _target_namespace && !(browsename.compare(_target_browsename))) {
                // Match found
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "%d" , (UA_UInt32) ref->nodeId.nodeId.identifier.numeric);
                matching_node_ids.push_back(ref->nodeId.nodeId);
            }
        }
    }

    UA_BrowseRequest_clear(&bReq);
    UA_BrowseResponse_clear(&bResp);
    return matching_node_ids;
}



int main(int argc, char* argv[]) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_Client* client = UA_Client_new();
    client_connection_establisher con_estab;
    UA_SessionState session_state = con_estab.establish_connection(client, 4840);
    if (session_state != UA_SESSIONSTATE_ACTIVATED) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing client session", __FUNCTION__);
        running = false;
        return UA_STATUSCODE_BAD;
    }

    std::vector<UA_NodeId> object_ids = browse(client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), 1, "Pump (Manual)");
    for (UA_NodeId nid : object_ids) {
        std::cout << "Found object id: " << nid.namespaceIndex << "," << nid.identifier.numeric << std::endl;
        std::vector<UA_NodeId> attribute_ids = browse(client, nid);
        for (UA_NodeId nid : attribute_ids) {
            std::cout << "Found attribure id: " << nid.namespaceIndex << "," << nid.identifier.numeric << std::endl;
            information_node_reader nreader;
            UA_StatusCode status = nreader.read_information_node(client, nid);
            if (status != UA_STATUSCODE_GOOD) {
                UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Failed to read information node");
                continue;
            }
            UA_String string_value = *(UA_String*) nreader.get_variant()->data;
            std::string new_str = std::string((char*) string_value.data, string_value.length);
            std::cout << "Attribute value: " << new_str << std::endl;
        }
    }

    UA_Client_delete(client);
    return 0;
}