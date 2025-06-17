#ifndef FILTERED_LOGGER_HPP
#define FILTERED_LOGGER_HPP

#include <open62541/server.h>
#include <stdarg.h>

typedef struct {
    UA_LogLevel min_level_;
    UA_LogCategory allowed_category_;
} custom_log_context;

class filtered_logger {
private:
    static void print_log(void* _log_context, UA_LogLevel _level, UA_LogCategory _category, const char* _msg, va_list _args);
    static void clear_logger(struct UA_Logger* _logger);
public:
    filtered_logger();
    ~filtered_logger();
    UA_Logger create_filtered_logger(UA_LogLevel _level, UA_LogCategory _category);
};

#endif // FILTERED_LOGGER_HPP