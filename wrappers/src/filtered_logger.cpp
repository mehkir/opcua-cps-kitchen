
#include "../include/filtered_logger.hpp"
#include <stdio.h>

filtered_logger::filtered_logger() {
}

filtered_logger::~filtered_logger() {
}

void
filtered_logger::print_log(void* _log_context, UA_LogLevel _level, UA_LogCategory _category,
                    const char* _msg, va_list _args) {
    custom_log_context* ctx = (custom_log_context *)_log_context;

    if (_level == ctx->min_level_ && _category == ctx->allowed_category_) {
        vfprintf(stdout, _msg, _args);
        fprintf(stdout, "\n");
    }
}

void
filtered_logger::clear_logger(struct UA_Logger *logger) {
    UA_free(logger->context);
}

UA_Logger
filtered_logger::create_filtered_logger(UA_LogLevel _level, UA_LogCategory _category) {
    UA_Logger logger;
    custom_log_context* ctx = (custom_log_context *)UA_malloc(sizeof(custom_log_context));
    ctx->min_level_ = _level;
    ctx->allowed_category_ = _category;

    logger.log = print_log;
    logger.context = ctx;
    logger.clear = clear_logger;
    return logger;
}
