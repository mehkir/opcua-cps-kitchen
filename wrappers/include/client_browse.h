#ifndef CLIENT_BROWSE_H
#define CLIENT_BROWSE_H

#include <open62541/client_highlevel.h>

#ifdef __cplusplus
extern "C" {
#endif

UA_BrowseResult
UA_Client_browse(UA_Client *client, const UA_ViewDescription *view,
                 UA_UInt32 requestedMaxReferencesPerNode,
                 const UA_BrowseDescription *nodesToBrowse);

#ifdef __cplusplus
}
#endif

#endif // CLIENT_BROWSE_H