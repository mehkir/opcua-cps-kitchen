/**
 * @file client_browse.h
 * @brief C linkage helper to perform a browse operation with custom parameters.
 *
 * Provides a thin wrapper declaration around open62541 high-level client browse
 * functionality so it can be used uniformly from C and C++ translation units.
 */
#ifndef CLIENT_BROWSE_H
#define CLIENT_BROWSE_H

#include <open62541/client_highlevel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Browse nodes starting from given browse descriptions.
 *
 * Wrapper around the open62541 browse service. The caller owns the returned
 * UA_BrowseResult and must call UA_BrowseResult_clear when done.
 *
 * @param client Connected OPC UA client.
 * @param view Optional view description (may be NULL) restricting the browse.
 * @param requestedMaxReferencesPerNode Limit for references per node (0 = server default).
 * @param nodesToBrowse Array of browse descriptions (terminated by array size in embedding context).
 * @return UA_BrowseResult Structure with results or diagnostics on failure.
 */
UA_BrowseResult
UA_Client_browse(UA_Client *client, const UA_ViewDescription *view,
                 UA_UInt32 requestedMaxReferencesPerNode,
                 const UA_BrowseDescription *nodesToBrowse);

#ifdef __cplusplus
}
#endif

#endif // CLIENT_BROWSE_H