#ifndef CLIENT_CONNECTION_ESTABLISHER_HPP
#define CLIENT_CONNECTION_ESTABLISHER_HPP

#include <open62541/client_highlevel.h>

class client_connection_establisher {
private:
    /* data */
public:
    client_connection_establisher(/* args */);
    ~client_connection_establisher();
    UA_SessionState establish_connection(UA_Client* _client, UA_UInt16 _server_port);
};




#endif // CLIENT_CONNECTION_ESTABLISHER_HPP