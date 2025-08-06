#ifndef CLIENT_CONNECTION_ESTABLISHER_HPP
#define CLIENT_CONNECTION_ESTABLISHER_HPP

#include <open62541/client_highlevel.h>
#include <unordered_map>
#include <string>

class client_connection_establisher {
private:
    UA_Client* client_;
public:
    client_connection_establisher(UA_Client*& _client);
    ~client_connection_establisher();
    bool establish_connection(std::string _server_endpoint);
    static bool test_connection(std::string _server_endpoint);
    bool reconnect();
};

#endif // CLIENT_CONNECTION_ESTABLISHER_HPP