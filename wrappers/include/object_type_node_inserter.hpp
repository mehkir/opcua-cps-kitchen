#ifndef OBJECT_TYPE_NODE_INSERTER_HPP
#define OBJECT_TYPE_NODE_INSERTER_HPP

#include <open62541/server.h>
#include <string>
#include <unordered_map>

class object_type_node_inserter {
    private:
        /**
         * The server instance the object type will be added to
         */
        UA_Server* server_;

        /**
         * The object id of the parent object
         */
        UA_NodeId parent_object_id_;

        /**
         * The object ids map containing the node ids of all objects added by the inserter including the parent object
         */
        std::unordered_map<std::string, UA_NodeId> object_ids_;

        /**
         * @brief Makes an attribute mandatory by its attribute id
         * 
         * @param _attribute_id 
         */
        void make_mandatory(UA_NodeId _attribute_id);

    public:
        /**
         * @brief Constructs a new object type node inserter object
         * 
         * @param _server 
         * @param _object_name 
         */
        object_type_node_inserter(UA_Server* _server, const char* _object_name);

        /**
         * @brief Destroys the object type node inserter object
         * 
         */
        ~object_type_node_inserter();

        /**
         * @brief Adds an attribute
         * 
         * @param _parent_object_name the object's name the attribute is added to
         * @param _attribute_name the attribute's name
         * @param _mandatory flag to determine whether the attribute is mandatory
         */
        void add_attribute(std::string _parent_object_name, const char* _attribute_name, bool _mandatory = true);

        /**
         * @brief Adds an object type attribute
         * 
         * @param _object_name the object's name
         */
        void add_object_type_attribute(const char* _object_name);

        /**
         * @brief Adds an object instance
         * 
         * @param _instance_name the instance name
         */
        void add_object_instance(const char* _instance_name, const char* _type_name);

        /**
         * @brief Constructor called when a new object type is instantiated
         * 
         * @param _server the server
         * @param _session_id the session id
         * @param _session_context the session context
         * @param _type_id the type id
         * @param _type_context the type context
         * @param _node_id the node id
         * @param _node_context the node context
         * @return UA_StatusCode the status code
         */
        static UA_StatusCode
        object_type_constructor(UA_Server* _server,
                                const UA_NodeId* _session_id, void* _session_context,
                                const UA_NodeId* _type_id, void* _type_context,
                                const UA_NodeId* _nodeId, void** _node_context);

        /**
         * @brief Adds the object type constructor
         * 
         * @param _server the server to add the constructor to
         * @param _object_id the object id to add the constructor for
         */
        void
        add_object_type_constructor(UA_Server* _server, UA_NodeId _object_id);

        /**
         * @brief Returns the object id by its name
         * 
         * @param _object_name the object's name
         */
        UA_NodeId get_object_id(std::string _object_name);

        /**
         * @brief Returns whether the given object id is known or not
         * 
         * @param _object_name the object's name
         */
        bool has_object(std::string _object_name);

};

#endif // OBJECT_TYPE_NODE_INSERTER_HPP