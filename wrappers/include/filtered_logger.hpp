/**
 * @file filtered_logger.hpp
 * @brief Create open62541 logger instances filtered by level and category.
 */
#ifndef FILTERED_LOGGER_HPP
#define FILTERED_LOGGER_HPP

#include <open62541/server.h>
#include <stdarg.h>

/**
 * @brief Context object specifying minimal log level and single allowed category.
 */
typedef struct {
    UA_LogLevel min_level_; /**< minimum log level accepted. */
    UA_LogCategory allowed_category_; /**< only this category is forwarded. */
} custom_log_context;

/**
 * @brief Factory for level/category filtered UA_Logger objects.
 */
class filtered_logger {
private:
    /**
     * @brief Filters the print function.
     * 
     * @param _log_context the log context.
     * @param _level the log level.
     * @param _category the log category.
     * @param _msg the message.
     * @param _args the message format args.
     */
    static void 
    print_log(void* _log_context, UA_LogLevel _level, UA_LogCategory _category, const char* _msg, va_list _args);

    /**
     * @brief Cleanup hook for allocated context.
     * 
     * @param _logger the logger to be cleared.
     */
    static void
    clear_logger(struct UA_Logger* _logger);
public:
    /**
     * @brief Constructs a new filtered logger object.
     * 
     */
    filtered_logger();

    /**
     * @brief Destroys the filtered logger object.
     * 
     */
    ~filtered_logger();
    /**
     * @brief Creates a filtered logger instance.
     * @param _level minimum log level.
     * @param _category category to allow (others ignored).
     * @return UA_Logger struct suitable for UA_ServerConfig.
     */
    UA_Logger create_filtered_logger(UA_LogLevel _level, UA_LogCategory _category);
};

#endif // FILTERED_LOGGER_HPP