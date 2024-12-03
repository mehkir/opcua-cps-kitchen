#include "../include/response_checker.hpp"
#include <stdexcept>

response_checker::response_checker(UA_CallResponse* _response) : response_(*_response) {
    if (_response == NULL)
        throw std::invalid_argument("response must not be null");
}

response_checker::~response_checker() {
}

bool response_checker::has_scalar_type(size_t _results_index, size_t _output_index, const UA_DataType* _type) {
    if (_output_index >= get_output_arguments_size(_results_index)) {
        throw std::invalid_argument("output_index is out of range");
        return false;
    }

    return UA_Variant_hasScalarType(&response_.results[_results_index].outputArguments[_output_index], _type);
}

size_t response_checker::get_results_size() {
    return response_.resultsSize;
}

size_t response_checker::get_output_arguments_size(size_t _results_index) {
    if (_results_index >= get_results_size()) {
        throw std::invalid_argument("results_index is out of range");
        return 0;
    }
    return response_.results[_results_index].outputArgumentsSize;
}

void* response_checker::get_data(size_t _results_index, size_t _output_index) {
    if (_output_index >= get_output_arguments_size(_results_index)) {
        throw std::invalid_argument("output_index is out of range");
        return NULL;
    }
    return response_.results[_results_index].outputArguments[_output_index].data;
}