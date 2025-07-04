#ifndef CLIENT_CONNECTION_ESTABLISHER_HPP
#define CLIENT_CONNECTION_ESTABLISHER_HPP

#include <open62541/client_highlevel.h>
#include <unordered_map>
#include <string>

class client_connection_establisher {
private:
    UA_Client* client_;
    static std::unordered_map<UA_Client*, client_connection_establisher*> client_map_;
    bool connected_;
public:
    client_connection_establisher(UA_Client* _client);
    ~client_connection_establisher();
    bool establish_connection(std::string _server_endpoint);
    void static connection_status_callback(UA_Client* _client, UA_SecureChannelState _channel_state, UA_SessionState _session_state, UA_StatusCode _connect_status);
    void on_connection_status(UA_Client* _client, UA_SecureChannelState _channel_state, UA_SessionState _session_state, UA_StatusCode _connect_status);
};




#endif // CLIENT_CONNECTION_ESTABLISHER_HPP