#ifndef OBJECT_TYPE_NODE_INSERTER_HPP
#define OBJECT_TYPE_NODE_INSERTER_HPP

#include <open62541/server.h>
#include <string>
#include <unordered_map>

class object_type_node_inserter {
    private:
        /**
         * @brief The server instance the object type will be added to
         */
        UA_Server* server_;

        /**
         * @brief The object id of the parent object type
         */
        UA_NodeId parent_object_type_id_;

        /**
         * @brief The object ids map containing the node ids of all object types added by this inserter including the parent object type
         */
        std::unordered_map<std::string, UA_NodeId> object_type_ids_;

        /**
         * @brief The instance ids map containing the node ids of all instances added by this inserter (add_object_instance)
         * 
         */
        std::unordered_map<std::string, UA_NodeId> instance_ids_;

        /**
         * @brief Makes an attribute mandatory by its attribute id
         * 
         * @param _attribute_id 
         */
        void make_mandatory(UA_NodeId _attribute_id);

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
         * @brief Returns whether the given object type's id is known or not
         * 
         * @param _object_type_name the object type name
         * @return true if known
         * @return false if unknown
         */
        bool has_object_type(std::string _object_type_name);

        /**
         * @brief Returns whether the given instance's id is known or not
         * 
         * @param _instance_name the instance name
         * @return true if known
         * @return false if unknown
         */
        bool
        has_instance(std::string _instance_name);

        /**
         * @brief Looks for the attribute node id
         * 
         * @param _instance_name the instance name whose attribute node id is to be searched for
         * @param _attribute_name the attribute name
         * @return UA_StatusCode the status code
         */
        UA_StatusCode find_attribute_node_id(std::string _instance_name, const char* _attribute_name, UA_NodeId& _node_id);

    public:
        /**
         * @brief Constructs a new object type node inserter
         * 
         * @param _server the server instance
         * @param _parent_object_type_name the name of the parent object type under which all attributes are added
         */
        object_type_node_inserter(UA_Server* _server, const char* _parent_object_type_name);

        /**
         * @brief Destroys the object type node inserter object
         * 
         */
        ~object_type_node_inserter();

        /**
         * @brief Adds an attribute
         * 
         * @param _parent_object_type_name the object type name the attribute is added to
         * @param _attribute_name the attribute's name
         * @param _mandatory flag to determine whether the attribute is mandatory
         */
        void add_attribute(std::string _parent_object_type_name, const char* _attribute_name, bool _mandatory = true);

        /**
         * @brief Adds an object sub type which inherits attributes from the parent object type
         * 
         * @param _object_type_name the object type name
         */
        void add_object_sub_type(const char* _object_type_name);

        /**
         * @brief Adds an instance of the given type
         * 
         * @param _instance_name the instance name
         * @param _type_object_name the object type name
         */
        void add_object_instance(const char* _instance_name, const char* _type_name);

        /**
         * @brief Adds the object type constructor
         * 
         * @param _server the server to add the constructor to
         * @param _object_type_id the object type id to add the constructor for
         */
        void
        add_object_type_constructor(UA_Server* _server, UA_NodeId _object_type_id);

        /**
         * @brief Returns the object type id by its name
         * 
         * @param _object_type_name the object's name
         */
        UA_NodeId get_object_type_id(std::string _object_type_name);

        /**
         * @brief Sets the scalar attribute object for the given instance
         * 
         * @param _instance_name the instance name
         * @param _attribute_name the attribute name
         * @param _value the value
         * @param _type_index the type index
         * @return UA_StatusCode the status code
         */
        UA_StatusCode set_scalar_attribute(std::string _instance_name, const char* _attribute_name, void* _value, UA_UInt32 _type_index);
};

#endif // OBJECT_TYPE_NODE_INSERTER_HPP