#ifndef RESPONSE_CHECKER_HPP
#define RESPONSE_CHECKER_HPP

#include <open62541/types_generated.h>

class response_checker {
private:
    const UA_CallResponse& response_;
public:
    response_checker(UA_CallResponse* _response);
    ~response_checker();
    bool has_scalar_type(size_t _results_index, size_t _output_index, const UA_DataType* _type);
    size_t get_results_size();
    size_t get_output_arguments_size(size_t _results_index);
    void* get_data(size_t _results_index, size_t _output_index);
    UA_StatusCode get_service_result();
};


#endif // RESPONSE_CHECKER_HPP