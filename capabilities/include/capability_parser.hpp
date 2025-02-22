#ifndef CAPABILITY_PARSER_HPP
#define CAPABILITY_PARSER_HPP

#include <unordered_set>
#include <string>

class capability_parser {
private:
    std::unordered_set<std::string> capabilities_;
public:
    capability_parser(std::string _capabilities_path);
    ~capability_parser();
};

#endif // CAPABILITY_PARSER_HPP