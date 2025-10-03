/**
 * @file robot_actions.hpp
 * @brief Action types and singleton registry for robot actions in the CPS Kitchen.
 *
 * Provides:
 * - Abstract base type for actions
 * - Concrete action variants (autonomous and recipe-timed)
 * - A singleton registry to query actions by name
 *
 * Duration values use cps_kitchen::duration_t (see types.hpp).
 */
#ifndef ROBOT_ACTIONS_HPP
#define ROBOT_ACTIONS_HPP

#include <mutex>
#include <unordered_map>
#include <string>
#include <memory>
#include "robot_tool.hpp"
#include "types.hpp"

#define RETOOLING_TIME 1LL

using namespace cps_kitchen;

/**
 * @brief Abstract base for any executable action.
 */
struct action {
    private:
    public:
        /**
         * @brief Returns the action name
         * 
         * @return std::string the action name string
         */
        virtual std::string get_name() const = 0;

        /**
         * @brief Destroys the action object
         * 
         */
        virtual ~action() = default;
};

/**
 * @brief Timed robot action with explicit ingredients and duration
 *
 * Represents an action the robot executes with a specific tool, an
 * ingredient descriptor, and a fixed duration.
 */
struct robot_action : public action {
    private:
        std::string name_; /**< the robot action name */
        robot_tool required_tool_; /**< the required tool */
        std::string ingredients_; /**< the required ingredients */
        duration_t duration_; /**< the action duration */
    public:
        /**
         * @brief Constructs a new robot action object
         * 
         * @param _name the robot action name
         * @param _required_tool the required tool
         * @param _ingredients the required ingredients
         * @param _duration the action duration
         */
        robot_action(std::string _name, robot_tool _required_tool, std::string _ingredients, duration_t _duration) : name_(_name), required_tool_(_required_tool), ingredients_(_ingredients), duration_(_duration) {
        }
        
        /**
         * @brief Destroys the robot action object
         * 
         */
        ~robot_action() {
        }

        /**
         * @brief Returns the name object
         * 
         * @return std::string the robot action name string
         */
        std::string
        get_name() const {
            return name_;
        }

        /**
         * @brief Returns the required robot tool
         * 
         * @return robot_tool the required robot tool
         */
        robot_tool
        get_required_tool() const {
            return required_tool_;
        }

        /**
         * @brief Returns the required ingredients
         * 
         * @return std::string the required ingredients string
         */
        std::string get_ingredients() const {
            return ingredients_;
        }

        /**
         * @brief Returns the action duration
         * 
         * @return duration_t the action duration
         */
        duration_t
        get_action_duration() const {
            return duration_;
        }
};

/**
 * @brief Timed autonomous action with fixed duration
 */
struct autonomous_action : public action {
    private:
        std::string name_; /**< the robot action name */
        robot_tool required_tool_; /**< the required tool */
        duration_t duration_; /**< the action duration */
    public:
        /**
         * @brief Constructs a new autonomous action
         * 
         * @param _name the robot action name
         * @param _required_tool the required tool
         * @param _duration the action duration
         */
        autonomous_action(std::string _name, robot_tool _required_tool, duration_t _duration) : name_(_name), required_tool_(_required_tool), duration_(_duration) {
        }

        /**
         * @brief Destroys the autonomous action
         * 
         */
        ~autonomous_action() {
        }

        /**
         * @brief Returns the action name
         * 
         * @return std::string the action name string
         */
        virtual std::string
        get_name() const {
            return name_;
        }

        /**
         * @brief Returns the required tool
         * 
         * @return robot_tool the required tool
         */
        robot_tool
        get_required_tool() const {
            return required_tool_;
        }

        /**
         * @brief Returns the action duration
         * 
         * @return duration_t the action duration
         */
        duration_t
        get_action_duration() const {
            return duration_;
        }
};

/**
 * @brief Action whose duration is determined by the recipe at runtime.
 */
struct recipe_timed_action : public action {
    private:
        std::string name_; /**< the action name */
        robot_tool required_tool_; /**< the required tool */
    public:
        recipe_timed_action(std::string _name, robot_tool _required_tool) : name_(_name), required_tool_(_required_tool) {
        }

        /**
         * @brief Destroys the recipe timed action
         * 
         */
        ~recipe_timed_action() {
        }

        /**
         * @brief Returns the action name
         * 
         * @return std::string the action name string
         */
        virtual std::string
        get_name() const {
            return name_;
        }

        /**
         * @brief Returns the required robot tool
         * 
         * @return robot_tool the required robot tool
         */
        robot_tool
        get_required_tool() const {
            return required_tool_;
        }
};

/**
 * @brief Singleton registry for known robot actions providing lookups by action name for autonomous and recipe-timed actions
 */
class robot_actions {
    public:
        /**
         * @brief Returns the singleton robot_actions instance
         * 
         * @return robot_actions* the robot actions address
         */
        static robot_actions* get_instance();

        /**
         * @brief Checks whether the given action exists in the registry
         * 
         * @param _action_name the action name
         * @return true if action exists
         * @return false if action doesn't exist
         */
        bool has_action(const std::string _action_name) const;

        /**
         * @brief Returns the action by name
         * 
         * @param _action_name the action name
         * @return std::shared_ptr<action> the action
         */
        std::shared_ptr<action> get_robot_action(const std::string _action_name);
    private:
        /**
         * @brief Constructs a new robot actions object
         * 
         */
        robot_actions();

        /**
         * @brief Destroys the robot actions object
         * 
         */
        ~robot_actions();

        /**
         * @brief The singleton robot_actions instance pointer
         * 
         */
        static robot_actions* instance_;

        /**
         * @brief The mutex ensuring the singleton instance
         * 
         */
        static std::mutex mutex_;

        /**
         * @brief The action registry
         * 
         */
        std::unordered_map<std::string, std::shared_ptr<action>> action_map_;
};

#endif // ROBOT_ACTIONS_HPP