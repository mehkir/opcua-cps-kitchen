#include "../include/information_node_writer.hpp"

information_node_writer::information_node_writer() {
}

information_node_writer::~information_node_writer() {
}

UA_StatusCode
information_node_writer::write_value(UA_Server* _server, UA_NodeId _node_id, void* _value, const UA_DataType* _type) {
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, _value, _type);
    UA_StatusCode status_code = UA_Server_writeValue(_server, _node_id, variant);
    UA_Variant_clear(&variant);
    return status_code;
}