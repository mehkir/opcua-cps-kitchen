#ifndef ROBOT_ACTIONS.HPP
#define ROBOT_ACTIONS.HPP

#include <mutex>
#include <unordered_map>
#include <string>
#include <memory>
#include "robot_tools.hpp"
#include "types.hpp"

using namespace cps_kitchen;

struct action {
    private:
    public:
        virtual std::string get_name() const = 0;
        virtual ~action() = default;
};

struct robot_action : public action {
    private:
        std::string name_;
        robot_tools required_tool_;
        std::string ingredients_;
        duration_t duration_;
    public:
        robot_action(std::string _name, robot_tools _required_tool, std::string _ingredients, duration_t _duration) : name_(_name), required_tool_(_required_tool), ingredients_(_ingredients), duration_(_duration) {
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

        std::string get_ingredients() const {
            return ingredients_;
        }
        
        duration_t
        get_action_duration() const {
            return duration_;
        }
};

struct autonomous_action : public action {
    private:
        std::string name_;
        robot_tools required_tool_;
        duration_t duration_;
    public:
        autonomous_action(std::string _name, robot_tools _required_tool, duration_t _duration) : name_(_name), required_tool_(_required_tool), duration_(_duration) {
        }

        ~autonomous_action() {
        }

        virtual std::string
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
};

struct recipe_timed_action : public action {
    private:
        std::string name_;
        robot_tools required_tool_;
    public:
        recipe_timed_action(std::string _name, robot_tools _required_tool) : name_(_name), required_tool_(_required_tool) {
        }

        ~recipe_timed_action() {
        }

        virtual std::string
        get_name() const {
            return name_;
        }

        robot_tools
        get_required_tool() const {
            return required_tool_;
        }
};

class robot_actions {
    public:
        static robot_actions* get_instance();
        bool has_action(const std::string _action_name) const;
        std::shared_ptr<action> get_robot_action(const std::string _action_name);
    private:
        robot_actions();
        ~robot_actions();
        static robot_actions* instance_;
        static std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<action>> action_map_;
};

#endif // ROBOT_ACTIONS