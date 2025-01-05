#ifndef TIME_ACTIONS.HPP
#define TIME_ACTIONS.HPP

#include <mutex>
#include <unordered_map>
#include "robot_tools.hpp"

struct time_action
{
    std::string name_;
    std::string required_robot_tool_name_;
    robot_tools required_robot_tool_enum_;
};


class time_actions {
    public:
        static time_actions* get_instance();
    private:
        time_actions();
        ~time_actions();
        static time_actions* instance_;
        static std::mutex mutex_;
        std::unordered_map<std::string, time_action> time_action_map_;
};

#endif // TIME_ACTIONS