#ifndef NODE_BROWSER_HELPER_HPP
#define NODE_BROWSER_HELPER_HPP

#include <open62541/client_highlevel.h>
#include <string>

struct object_method_info {
    public:
        UA_NodeId object_id_;
        UA_NodeId method_id_;

        object_method_info() : object_id_(UA_NODEID_NULL), method_id_(UA_NODEID_NULL) {
        }

        ~object_method_info() {
        }

        bool operator==(const object_method_info& other) const {
            return UA_NodeId_equal(&object_id_, &other.object_id_) && UA_NodeId_equal(&method_id_, &other.method_id_);
        }

        bool operator!=(const object_method_info& other) const {
            return !(*this == other);
        }
};

const object_method_info OBJECT_METHOD_INFO_NULL = object_method_info();

class node_browser_helper {
private:
public:
    node_browser_helper();
    ~node_browser_helper();

    /**
     * @brief Returns the method id of the first instance with the given object type
     * 
     * @param _client the client
     * @param _object_type_name the object type browsename
     * @param _method_name the method browsename
     * @return object_method_info the node id of the instance and method if found, else UA_NODEID_NULL for both
     */
    object_method_info get_method_id(UA_Client* _client, std::string _object_type_name, std::string _method_name);
};

#endif // NODE_BROWSER_HELPER_HPP