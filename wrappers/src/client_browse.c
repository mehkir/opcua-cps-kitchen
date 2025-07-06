#include "../include/client_browse.h"

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