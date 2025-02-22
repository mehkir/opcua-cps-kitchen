#ifndef CAPABILITY_PARSER_HPP
#define CAPABILITY_PARSER_HPP

#include <unordered_set>
#include <string>
#include <types.hpp>

using namespace cps_kitchen;

class capability_parser {
private:
    std::unordered_set<std::string> capabilities_;
public:
    capability_parser(std::string _capabilities_path, position_t _robot_position);
    ~capability_parser();
    bool is_capable_to(std::string _action_name);
};

#endif // CAPABILITY_PARSER_HPP