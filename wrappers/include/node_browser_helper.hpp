/**
 * @file node_browser_helper.hpp
 * @brief Helper types and functions to locate methods and attributes of object instances by type and browse names.
 */
#ifndef NODE_BROWSER_HELPER_HPP
#define NODE_BROWSER_HELPER_HPP

#include <open62541/client_highlevel.h>
#include <string>

/**
 * @brief Holds object and method ids for a discovered method.
 */
struct object_method_info {
    public:
        UA_NodeId object_id_; /**< the object/parent id */
        UA_NodeId method_id_; /**< the method id */

        /**
         * @brief Constructs a new object method info object.
         * 
         */
        object_method_info() : object_id_(UA_NODEID_NULL), method_id_(UA_NODEID_NULL) {
        }

        /**
         * @brief Destroys the object method info object.
         * 
         */
        ~object_method_info() {
        }

        /**
         * @brief Compares an object method info object with another for equality.
         * 
         * @param other the other object method info.
         * @return true if equal.
         * @return false if unequal.
         */
        bool operator==(const object_method_info& other) const {
            return UA_NodeId_equal(&object_id_, &other.object_id_) && UA_NodeId_equal(&method_id_, &other.method_id_);
        }

        /**
         * @brief Compares an object method info object with another for inequality.
         * 
         * @param other the other object method info.
         * @return true if unequal.
         * @return false if equal.
         */
        bool operator!=(const object_method_info& other) const {
            return !(*this == other);
        }
};

const object_method_info OBJECT_METHOD_INFO_NULL = object_method_info(); /**< NULL representation of object method info. */

/**
 * @brief Browse-based utilities for locating instances, methods and attributes.
 */
class node_browser_helper {
private:
public:
    /**
     * @brief Constructs a new node browser helper object.
     * 
     */
    node_browser_helper();

    /**
     * @brief Destroys the node browser helper object.
     * 
     */
    ~node_browser_helper();

    /**
     * @brief Returns the method and object id of the first instance with the given object type.
     * 
     * @param _client the client.
     * @param _object_type_name the object type browsename.
     * @param _method_name the method browsename.
     * @return object_method_info the node id of the instance and method if found, else UA_NODEID_NULL for both.
     */
    object_method_info get_method_id(UA_Client* _client, std::string _object_type_name, std::string _method_name);

    /**
     * @brief Returns the attribute id of the first instance with the given object type.
     * 
     * @param _client the client.
     * @param _object_type_name the object type browsename.
     * @param _attribute_name the attribute browsename.
     * @return UA_NodeId the attribute id.
     */
    UA_NodeId get_attribute_id(UA_Client* _client, std::string _object_type_name, std::string _attribute_name);

    /**
     * @brief Returns whether an instance of the given object type is present.
     * 
     * @param _client the client.
     * @param _object_type_name the object type browsename.
     * @return true if instance of given object type is present.
     * @return false if instance of given object type is not present.
     */
    bool has_instance(UA_Client* _client, std::string _object_type_name);

    /**
     * @brief Returns the method and object id of the first instance with the given object type.
     * 
     * @param _server_endpoint the server endpoint.
     * @param _object_type_name the object type browsename.
     * @param _method_name the method browsename.
     * @return object_method_info the node id of the instance and method if found, else UA_NODEID_NULL for both.
     */
    object_method_info get_method_id(std::string _server_endpoint, std::string _object_type_name, std::string _method_name);

    /**
     * @brief Returns the attribute id of the first instance with the given object type.
     * 
     * @param _server_endpoint the server endpoint.
     * @param _object_type_name the object type browsename.
     * @param _attribute_name the attribute browsename.
     * @return UA_NodeId the attribute id.
     */
    UA_NodeId get_attribute_id(std::string _server_endpoint, std::string _object_type_name, std::string _attribute_name);

    /**
     * @brief Returns whether an instance of the given object type is present.
     * 
     * @param _server_endpoint the server endpoint.
     * @param _object_type_name the object type browsename.
     * @return true if instance of given object type is present.
     * @return false if instance of given object type is not present.
     */
    bool has_instance(std::string _server_endpoint, std::string _object_type_name);
};

#endif // NODE_BROWSER_HELPER_HPP