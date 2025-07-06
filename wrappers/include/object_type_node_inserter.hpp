#ifndef OBJECT_TYPE_NODE_INSERTER_HPP
#define OBJECT_TYPE_NODE_INSERTER_HPP

#include <open62541/server.h>
#include <string>
#include <unordered_map>
#include <vector>

struct method_arguments {
    private:
        std::vector<UA_Argument> input_arguments_;
        std::vector<UA_Argument> output_arguments_;

        void initialize_argument(std::string _description, std::string _name, UA_UInt32 _type_index, UA_Argument& _argument) {
            UA_Argument_init(&_argument);
            _argument.description = UA_LOCALIZEDTEXT_ALLOC(const_cast<char*>("en-US"), const_cast<char*>(_description.c_str()));
            _argument.name = UA_STRING_ALLOC(const_cast<char*>(_name.c_str()));
            _argument.dataType = UA_TYPES[_type_index].typeId;
            _argument.valueRank = UA_VALUERANK_ANY;
        }
    public:
        method_arguments(){
        }

        ~method_arguments(){
            for (auto& arg : input_arguments_) {
                UA_Argument_clear(&arg);
            }
            for (auto& arg : output_arguments_) {
                UA_Argument_clear(&arg);
            }
        }

        void add_input_argument(std::string _description, std::string _name, UA_UInt32 _type_index) {
            UA_Argument argument;
            initialize_argument(_description, _name, _type_index, argument);
            input_arguments_.push_back(argument);
        }

        void add_output_argument(std::string _description, std::string _name, UA_UInt32 _type_index) {
            UA_Argument argument;
            initialize_argument(_description, _name, _type_index, argument);
            output_arguments_.push_back(argument);
        }

        std::vector<UA_Argument> get_input_arguments() {
            return input_arguments_;
        }

        std::vector<UA_Argument> get_output_arguments() {
            return output_arguments_;
        }
};

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
         * @brief Makes an attribute or method mandatory by its node id
         * 
         * @param _node_id the node id of the attribute or method
         * @return UA_StatusCode the status code
         */
        UA_StatusCode make_mandatory(UA_NodeId _node_id);

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
        bool has_instance(std::string _instance_name);

        /**
         * @brief Looks for the child node id of the given instance
         * 
         * @param _instance_name the instance name whose attribute node id is to be searched for
         * @param _child_name the child name
         * @return UA_StatusCode the status code
         */
        UA_StatusCode find_child_node_id(std::string _instance_name, const char* _child_name, UA_NodeId& _node_id);

        /**
         * @brief Sets the attribute for the given instance
         * 
         * @param _instance_name the instance name
         * @param _attribute_name the attribute name
         * @param _value the value
         * @return UA_StatusCode the status code
         */
        UA_StatusCode set_attribute(std::string _instance_name, const char* _attribute_name, UA_Variant& _value);

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
         * @param _parent_object_type_name the object type name the attribute will be added to
         * @param _attribute_name the attribute's name
         * @param _mandatory flag to determine whether the attribute is mandatory
         * @return UA_StatusCode the status code
         */
        UA_StatusCode add_attribute(std::string _parent_object_type_name, const char* _attribute_name, bool _mandatory = true);

        /**
         * @brief Adds a method
         * 
         * @param _parent_object_type_name the object type to which the method will be added
         * @param _method_name the method name
         * @param _method_callback the method callback
         * @param _method_arguments the method arguments
         * @param _node_context the node context
         * @param _mandatory flag to determine whether the attribute is mandatory
         * @return UA_StatusCode the status code
         */
        UA_StatusCode add_method(std::string _parent_object_type_name, const char* _method_name, UA_MethodCallback _method_callback, method_arguments& _method_arguments, void* _node_context, bool _mandatory = true);

        /**
         * @brief Adds an object sub type which inherits attributes from the parent object type
         * 
         * @param _object_type_name the object type name
         * @return UA_StatusCode the status code
         */
        UA_StatusCode add_object_sub_type(const char* _object_type_name);

        /**
         * @brief Adds an instance of the given type
         * 
         * @param _instance_name the instance name
         * @param _type_object_name the object type name
         * @param _parent_node_id the parent node id under which the instance will be added
         * @param _reference_type the reference type this object is referenced by its parent node
         * @return UA_StatusCode the status code
         */
        UA_StatusCode add_object_instance(const char* _instance_name, const char* _type_name, UA_NodeId _parent_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NodeId _reference_type = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES));

        /**
         * @brief Adds the object type constructor
         * 
         * @param _server the server to add the constructor to
         * @param _object_type_id the object type id to add the constructor for
         * @return UA_StatusCode the status code
         */
        UA_StatusCode add_object_type_constructor(UA_Server* _server, UA_NodeId _object_type_id);

        /**
         * @brief Returns the object type id by its name
         * 
         * @param _object_type_name the object's name
         * @return UA_NodeId the object's node id
         */
        UA_NodeId get_object_type_id(std::string _object_type_name);

        /**
         * @brief Returns the instance id by its name
         * 
         * @param _instance_name the instance name
         * @return UA_NodeId the instance's node id
         */
        UA_NodeId get_instance_id(std::string _instance_name);

        /**
         * @brief Sets the scalar attribute for the given instance
         * 
         * @param _instance_name the instance name
         * @param _attribute_name the attribute name
         * @param _value the value
         * @param _type_index the type index
         * @return UA_StatusCode the status code
         */
        UA_StatusCode set_scalar_attribute(std::string _instance_name, const char* _attribute_name, void* _value, UA_UInt32 _type_index);

        /**
         * @brief Sets the array attribute for the given instance
         * 
         * @param _instance_name the instance name
         * @param _attribute_name the attribute name
         * @param _array the array
         * @param _array_size the array size
         * @param _type_index the type index
         * @return UA_StatusCode the status code
         */
        UA_StatusCode set_array_attribute(std::string _instance_name, const char* _attribute_name, void* _array, size_t _array_size, UA_UInt32 _type_index);

        /**
         * @brief Gets the attribute for the given instance
         * 
         * @param _instance_name the instance name
         * @param _attribute_name the attribute name
         * @param _value where the attribute value is stored
         * @return UA_StatusCode the status code
         */
        UA_StatusCode get_attribute(std::string _instance_name, const char* _attribute_name, UA_Variant& _value);
};

#endif // OBJECT_TYPE_NODE_INSERTER_HPP