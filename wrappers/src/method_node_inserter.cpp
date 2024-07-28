#include "../include/method_node_inserter.hpp"
#include <open62541/plugin/log_stdout.h>

method_node_inserter::method_node_inserter() : is_method_node_added_(false) {
}

method_node_inserter::~method_node_inserter() {
    input_arguments_.clear();
    output_arguments_.clear();
}

void method_node_inserter::add_input_argument(std::string _description, std::string _name, UA_UInt32 _type_index) {
    if (is_method_node_added_) {
        return;
    }
    UA_Argument input_argument;
    UA_Argument_init(&input_argument);
    input_argument.description = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_description.c_str()));
    input_argument.name = UA_STRING(const_cast<char*>(_name.c_str()));
    input_argument.dataType = UA_TYPES[_type_index].typeId;
    input_argument.valueRank = UA_VALUERANK_SCALAR;
    input_arguments_.push_back(input_argument);
}

void method_node_inserter::add_output_argument(std::string _description, std::string _name, UA_UInt32 _type_index) {
    if (is_method_node_added_) {
        return;
    }
    UA_Argument output_argument;
    UA_Argument_init(&output_argument);
    output_argument.description = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(_description.c_str()));
    output_argument.name = UA_STRING(const_cast<char*>(_name.c_str()));
    output_argument.dataType = UA_TYPES[_type_index].typeId;
    output_argument.valueRank = UA_VALUERANK_SCALAR;
    output_arguments_.push_back(output_argument);
}

UA_StatusCode method_node_inserter::add_method_node(UA_Server* _server, UA_NodeId _method_node_id, std::string _browse_name, UA_MethodCallback _method_callback) {
    UA_StatusCode status_code = UA_STATUSCODE_BAD;
    if (is_method_node_added_) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Method node already added");
        return status_code;
    }
    method_attributes_ = UA_MethodAttributes_default;
    std::string description = "desc.:" + _browse_name;
    std::string display_name = "disp.:" + _browse_name;
    method_attributes_.description = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(description.c_str()));
    method_attributes_.displayName = UA_LOCALIZEDTEXT("en-US", const_cast<char*>(display_name.c_str()));
    method_attributes_.executable = true;
    method_attributes_.userExecutable = true;
    status_code = UA_Server_addMethodNode(_server, _method_node_id,
                            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                            UA_QUALIFIEDNAME(1, const_cast<char*>(_browse_name.c_str())),
                            method_attributes_, _method_callback,
                            input_arguments_.size(), input_arguments_.data(), output_arguments_.size(), output_arguments_.data(), NULL, NULL);
    is_method_node_added_ = true;
    return status_code;
}