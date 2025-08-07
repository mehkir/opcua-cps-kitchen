#ifndef CLIENT_CONNECTION_ESTABLISHER_HPP
#define CLIENT_CONNECTION_ESTABLISHER_HPP

#include <open62541/client_highlevel.h>
#include <unordered_map>
#include <string>

class client_connection_establisher {
private:
public:
    client_connection_establisher();
    ~client_connection_establisher();

    /**
     * @brief Retries to establish a connection to a server with a new client. Ensure the pointer is deleted and is null.
     * 
     * @param _client the client pointer where the new created one's adress is stored
     * @param _server_endpoint the server endpoint
     * @return true if connection is established successfully
     * @return false if connection could not be established, _client is set to nullptr
     */
    bool
    establish_connection_retry(UA_Client*& _client, std::string _server_endpoint);

    /**
     * @brief Establishes a connection to a server with a new client. You must ensure that the pointer is deleted and is null.
     * 
     * @param _client the client pointer where the new created one's adress is stored
     * @param _server_endpoint the server endpoint
     * @return true if connection is established successfully
     * @return false if connection could not be established, _client is set to nullptr
     */
    bool establish_connection(UA_Client*& _client, std::string _server_endpoint);

    /**
     * @brief Tests if the a connection to the given endpoint can be established.
     * 
     * @param _server_endpoint the server endpoint
     * @return true if connection is established successfully
     * @return false false if connection could not be established
     */
    static bool 
    test_connection(std::string _server_endpoint);
};

#endif // CLIENT_CONNECTION_ESTABLISHER_HPP