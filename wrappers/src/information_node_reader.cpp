#include "../include/information_node_reader.hpp"

information_node_reader::information_node_reader() {
}

information_node_reader::~information_node_reader() {
    UA_Variant_clear(&variant_);
}

UA_StatusCode information_node_reader::read_information_node(UA_Client* _client, UA_NodeId _node_id) {
    UA_Variant_clear(&variant_);
    UA_Variant_init(&variant_);
    return UA_Client_readValueAttribute(_client, _node_id, &variant_);
}

UA_Variant* information_node_reader::get_variant() {
    return &variant_;
}