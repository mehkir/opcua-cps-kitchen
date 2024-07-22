#include "../include/method_node_inserter.hpp"

method_node_inserter::method_node_inserter() {
}

method_node_inserter::~method_node_inserter() {
}

void method_node_inserter::add_input_argument(std::string _description, std::string _name, UA_UInt32 _type_index) {

}

void method_node_inserter::add_output_argument(std::string _description, std::string _name, UA_UInt32 _type_index) {

}

UA_StatusCode method_node_inserter::add_method_node(UA_Server* _server, std::string _method_node_id, std::string _browse_name, UA_MethodCallback _method_callback) {
    return UA_STATUSCODE_GOOD;
}