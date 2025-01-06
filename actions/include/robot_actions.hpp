#ifndef ROBOT_ACTIONS.HPP
#define ROBOT_ACTIONS.HPP

#include <mutex>
#include <unordered_map>
#include <string>
#include "robot_tools.hpp"
#include "types.hpp"

using namespace cps_kitchen;

struct robot_action {
    public:
        robot_action(std::string _name, robot_tools _required_tool, duration_t _duration) : name_(_name), required_tool_(_required_tool), duration_(_duration), recipe_timed_(false) {
        }
        
        robot_action(std::string _name, robot_tools _required_tool) : name_(_name), required_tool_(_required_tool), duration_(0), recipe_timed_(true) {
        }
        
        ~robot_action() {
        }

        std::string
        get_name() const {
            return name_;
        }

        robot_tools
        get_required_tool() const {
            return required_tool_;
        }

        duration_t
        get_action_duration() const {
            return duration_;
        }

        bool
        is_recipe_timed() const {
            return recipe_timed_;
        }
    private:
        std::string name_;
        robot_tools required_tool_;
        duration_t duration_;
        bool recipe_timed_;
};

    // public:
    //     robot_action() {
    //     }
    //     ~robot_action() {
    //     }
    // prívate:
    //     std::string name_;
    //     robot_tools required_robot_tool_;
    //     duration_t duration_;

class robot_actions {
    public:
        static robot_actions* get_instance();
    private:
        robot_actions();
        ~robot_actions();
        static robot_actions* instance_;
        static std::mutex mutex_;
        std::unordered_map<std::string, robot_action> action_map_;
};

#endif // ROBOT_ACTIONS