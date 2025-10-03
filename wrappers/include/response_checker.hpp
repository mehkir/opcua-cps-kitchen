/**
 * @file response_checker.hpp
 * @brief Convenience wrapper for inspecting and validating UA_CallResponse results.
 */
#ifndef RESPONSE_CHECKER_HPP
#define RESPONSE_CHECKER_HPP

#include <open62541/types_generated.h>

class response_checker {
private:
    const UA_CallResponse& response_;
public:
    /**
     * @brief Constructs a new response checker instance.
     * 
     * @param _response the response to be checked.
     */
    response_checker(UA_CallResponse* _response);

    /**
     * @brief Destructs the response checker instance.
     * 
     */
    ~response_checker();

    /**
     * @brief Checks if the scalar value at the given indicies equals the given data type.
     * 
     * @param _results_index the results index for the output array in the results array.
     * @param _output_index the index of the data in the output array.
     * @param _type the given type to compare against.
     * @return true if the scalar value equals the data type.
     * @return false if the scalar value does not equal the data type.
     */
    bool
    has_scalar_type(size_t _results_index, size_t _output_index, const UA_DataType* _type);

    /**
     * @brief Returns the size of the results array.
     * 
     * @return size_t the size of the results array.
     */
    size_t
    get_results_size();

    /**
     * @brief Returns the size of the output array at the given results array index.
     * 
     * @param _results_index the results index for the output array in the results array.
     * @return size_t the size of the output array.
     */
    size_t
    get_output_arguments_size(size_t _results_index);

    /**
     * @brief Returns the pointer to the output data at the given indices.
     * 
     * @param _results_index the results index for the output array in the results array.
     * @param _output_index the index of the data in the output array.
     * @return void* the pointer to the data at the given indices.
     */
    void*
    get_data(size_t _results_index, size_t _output_index);

    /**
     * @brief Returns the service result of the response.
     * 
     * @return UA_StatusCode the status code.
     */
    UA_StatusCode
    get_service_result();
};


#endif // RESPONSE_CHECKER_HPP