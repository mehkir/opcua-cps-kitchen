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

/* Convenience method copied from v1.4.12 */
UA_BrowseResult
UA_Client_browse(UA_Client *client, const UA_ViewDescription *view,
                 UA_UInt32 requestedMaxReferencesPerNode,
                 const UA_BrowseDescription *nodesToBrowse) {
    UA_BrowseResult res;
    UA_BrowseRequest request;
    UA_BrowseResponse response;
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    if(!nodesToBrowse) {
        retval = UA_STATUSCODE_BADINTERNALERROR;
        goto error;
    }

    /* Set up the request */
    UA_BrowseRequest_init(&request);
    if(view)
        request.view = *view;
    request.requestedMaxReferencesPerNode = requestedMaxReferencesPerNode;
    request.nodesToBrowse = (UA_BrowseDescription*)(uintptr_t)nodesToBrowse;
    request.nodesToBrowseSize = 1;

    /* Call the service */
    response = UA_Client_Service_browse(client, request);
    retval = response.responseHeader.serviceResult;
    if(retval == UA_STATUSCODE_GOOD && response.resultsSize != 1)
        retval = UA_STATUSCODE_BADUNEXPECTEDERROR;
    if(UA_StatusCode_isBad(retval))
        goto error;

    /* Return the result */
    res = response.results[0];
    response.resultsSize = 0;
    UA_BrowseResponse_clear(&response);
    return res;

 error:
    UA_BrowseResponse_clear(&response);
    UA_BrowseResult_init(&res);
    res.statusCode = retval;
    return res;
}

UA_NodeId browse_object_type(UA_Client* _client, UA_NodeId _start_node_id, std::string _target_browsename) {
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
        if (!browse_name.compare(_target_browsename))
            matching_node_id = ref->nodeId.nodeId;
    }

    UA_BrowseResult_clear(&bres);
    return matching_node_id;
}

void browse_objects(UA_Client* _client, UA_BrowseResult& _browse_result) {
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

void browse_methods(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result) {
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

void browse_objects(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result) {
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

void browse_attributes(UA_Client* _client, UA_NodeId _instance_id, UA_BrowseResult& _browse_result) {
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

/* Returns the node id of the instance if its type definition equals the target type */
UA_NodeId browse_instance(UA_Client* _client, UA_NodeId _start_node_id, UA_NodeId _target_type_node_id) {
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = _start_node_id;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASTYPEDEFINITION);
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
    client_connection_establisher con_estab(client);
    bool connected = con_estab.establish_connection("opc.tcp://localhost:" + std::to_string(6000));
    if (!connected) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SESSION, "%s: Error establishing client session", __FUNCTION__);
        running = false;
        return UA_STATUSCODE_BAD;
    }
    /* Check if server has object type definition*/
    std::string target_browse_name("ConveyorType");
    UA_NodeId object_type_id = browse_object_type(client, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), target_browse_name);
    std::cout << target_browse_name << " " << "Node id(" << object_type_id.namespaceIndex << "," << object_type_id.identifier.numeric << ")" << std::endl;
    /* Filter objects which equal the type definition */
    UA_BrowseResult browse_objects_result;
    browse_objects(client, browse_objects_result);
    for (size_t i = 0; i < browse_objects_result.referencesSize; i++) {
        const UA_ReferenceDescription* reference_description = &browse_objects_result.references[i];
        if (UA_NodeId_equal(&reference_description->typeDefinition.nodeId, &object_type_id)) {
            UA_QualifiedName instance_browse_name = reference_description->browseName;
            std::string bn((char*) instance_browse_name.name.data, instance_browse_name.name.length);
            std::cout << bn << " Node id(" << reference_description->nodeId.nodeId.namespaceIndex << "," << reference_description->nodeId.nodeId.identifier.numeric << ")" << std::endl;
            /* Browse methods */
            UA_BrowseResult browse_methods_result;
            browse_methods(client, reference_description->nodeId.nodeId, browse_methods_result);
            for (size_t i = 0; i < browse_methods_result.referencesSize; i++) {
                const UA_ReferenceDescription* reference_description = &browse_methods_result.references[i];
                UA_QualifiedName method_browse_name = reference_description->browseName;
                std::string bn((char*) method_browse_name.name.data, method_browse_name.name.length);
                std::cout << "\t" << bn << " Node id(" << reference_description->nodeId.nodeId.namespaceIndex << "," << reference_description->nodeId.nodeId.identifier.numeric << ")" << std::endl;
            }
            UA_BrowseResult_clear(&browse_methods_result);
            /* Browse attributes */
            UA_BrowseResult browse_attributes_result;
            browse_attributes(client, reference_description->nodeId.nodeId, browse_attributes_result);
            for (size_t i = 0; i < browse_attributes_result.referencesSize; i++) {
                const UA_ReferenceDescription* reference_description = &browse_attributes_result.references[i];
                UA_QualifiedName attribute_browse_name = reference_description->browseName;
                std::string bn((char*) attribute_browse_name.name.data, attribute_browse_name.name.length);
                std::cout << "\t" << bn << " Node id(" << reference_description->nodeId.nodeId.namespaceIndex << "," << reference_description->nodeId.nodeId.identifier.numeric << ")" << std::endl;
            }
            UA_BrowseResult_clear(&browse_attributes_result);
            /* Browse object attributes */
            UA_BrowseResult browse_object_attributes_result;
            browse_objects(client, reference_description->nodeId.nodeId, browse_object_attributes_result);
            for (size_t i = 0; i < browse_object_attributes_result.referencesSize; i++) {
                const UA_ReferenceDescription* reference_description = &browse_object_attributes_result.references[i];
                UA_QualifiedName object_browse_name = reference_description->browseName;
                std::string bn((char*) object_browse_name.name.data, object_browse_name.name.length);
                std::cout << "\t" << bn << " Node id(" << reference_description->nodeId.nodeId.namespaceIndex << "," << reference_description->nodeId.nodeId.identifier.numeric << ")" << std::endl;
                /* Browse methods */
                UA_BrowseResult browse_methods_result;
                browse_methods(client, reference_description->nodeId.nodeId, browse_methods_result);
                for (size_t i = 0; i < browse_methods_result.referencesSize; i++) {
                    const UA_ReferenceDescription* reference_description = &browse_methods_result.references[i];
                    UA_QualifiedName method_browse_name = reference_description->browseName;
                    std::string bn((char*) method_browse_name.name.data, method_browse_name.name.length);
                    std::cout << "\t" << bn << " Node id(" << reference_description->nodeId.nodeId.namespaceIndex << "," << reference_description->nodeId.nodeId.identifier.numeric << ")" << std::endl;
                }
                UA_BrowseResult_clear(&browse_methods_result);
                /* Browse attributes */
                UA_BrowseResult browse_attributes_result;
                browse_attributes(client, reference_description->nodeId.nodeId, browse_attributes_result);
                for (size_t i = 0; i < browse_attributes_result.referencesSize; i++) {
                    const UA_ReferenceDescription* reference_description = &browse_attributes_result.references[i];
                    UA_QualifiedName attribute_browse_name = reference_description->browseName;
                    std::string bn((char*) attribute_browse_name.name.data, attribute_browse_name.name.length);
                    std::cout << "\t" << bn << " Node id(" << reference_description->nodeId.nodeId.namespaceIndex << "," << reference_description->nodeId.nodeId.identifier.numeric << ")" << std::endl;
                }
                UA_BrowseResult_clear(&browse_attributes_result);
            }
            
            UA_BrowseResult_clear(&browse_object_attributes_result);
            
        }
    }
    UA_BrowseResult_clear(&browse_objects_result);
    UA_Client_delete(client);
    return 0;
}