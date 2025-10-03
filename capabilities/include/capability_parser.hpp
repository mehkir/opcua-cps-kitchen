/**
 * @file capability_parser.hpp
 * @brief Declares the capability_parser for loading and querying robot capabilities.
 *
 * @details
 * Parses a JSON capabilities file located relative to the running executable,
 * validates listed actions against robot_actions, and exposes query helpers.
 */
#ifndef CAPABILITY_PARSER_HPP
#define CAPABILITY_PARSER_HPP

#include <unordered_set>
#include <string>
#include <types.hpp>

using namespace cps_kitchen;

class capability_parser {
private:
    std::unordered_set<std::string> capabilities_; /**< the capabilities set */
public:
    /**
     * @brief Constructs a new capability parser object
     * 
     * @param _capabilities_file_name the capabilities file name
     */
    capability_parser(std::string _capabilities_file_name);

    /**
     * @brief Destroys the capability parser object
     * 
     */
    ~capability_parser();

    /**
     * @brief Checks whether the given action is present in the capabilities
     * 
     * @param _action_name the action name to check for
     * @return true when action is present
     * @return false when action is not available
     */
    bool is_capable_to(std::string _action_name);

    /**
     * @brief Returns a copy of the capabilities set
     * 
     * @return std::unordered_set<std::string> the capabilities set
     */
    std::unordered_set<std::string> get_capabilities();
};

#endif // CAPABILITY_PARSER_HPP