/**
 * @file recipe_parser.hpp
 * @brief Declarations for building executable cooking plans from JSON recipes.
 *
 * The recipe_parser reads recipes.json located one directory above the binary’s
 * directory and validates each instruction to build a queue of robot action steps.
 *
 * It computes:
 * - cooking_time: total of all action durations
 * - retooling_time: adds RETOOLING_TIME when consecutive actions require different tools
 */
#ifndef RECIPE_PARSER_HPP
#define RECIPE_PARSER_HPP

#include <queue>

#include "robot_actions.hpp"

/**
 * @brief A recipe object representing a recipe's details.
 * 
 */
struct recipe {
    private:
        recipe_id_t recipe_id_; /**< the recipe id. */
        std::string dish_name_; /**< the dish name. */
        std::queue<robot_action> action_queue_; /**< the action queue. */
        duration_t cooking_time_; /**< the cooking time. */
        duration_t retooling_time_; /**< the retooling time. */
    public:
        recipe(recipe_id_t _recipe_id, std::string _dish_name, std::queue<robot_action> _action_queue, duration_t _cooking_time, duration_t _retooling_time) : recipe_id_(_recipe_id), dish_name_(_dish_name), action_queue_(_action_queue), cooking_time_(_cooking_time), retooling_time_(_retooling_time){
        }

        /**
         * @brief Returns the recipe id.
         * 
         * @return recipe_id_t the recipe id.
         */
        recipe_id_t get_recipe_id() const {
            return recipe_id_;
        }

        /**
         * @brief Returns the dish name.
         * 
         * @return std::string the dish name string.
         */
        std::string get_dish_name() const {
            return dish_name_;
        }

        /**
         * @brief Returns the action queue.
         * 
         * @return std::queue<robot_action> the action queue.
         */
        std::queue<robot_action> get_action_queue() const {
            return action_queue_;
        }

        /**
         * @brief Returns the cooking time.
         * 
         * @return duration_t the cooking time.
         */
        duration_t get_cooking_time() const {
            return cooking_time_;
        }

        /**
         * @brief Returns the retooling time for consecutive retoolings.
         * 
         * @return duration_t the retooling time.
         */
        duration_t get_retooling_time() const {
            return retooling_time_;
        }

        /**
         * @brief Returns the overall time calculated by cooking time plus retooling time.
         * 
         * @return duration_t cooking time plus retooling time.
         */
        duration_t get_overall_time() const {
            return cooking_time_ + retooling_time_;
        }

};

class recipe_parser {
    private:
        std::unordered_map<cps_kitchen::recipe_id_t, std::unique_ptr<recipe>> recipe_map_; /**< the recipe map.  */
    public:
        /**
         * @brief Constructs a new recipe parser object.
         * 
         */
        recipe_parser();

        /**
         * @brief Destroys the recipe parser object.
         * 
         */
        ~recipe_parser();

        /**
         * @brief Returns whether a recipe with the given id exists.
         * 
         * @param _recipe_id the recipe id.
         * @return true when the recipe exists.
         * @return false when the recipe doesn't exist.
         */
        bool has_recipe(const cps_kitchen::recipe_id_t _recipe_id) const;

        /**
         * @brief Returns the recipe object for the given recipe id.
         * 
         * @param _recipe_id the recipe id.
         * @return recipe the recipe for the given id.
         */
        recipe get_recipe(cps_kitchen::recipe_id_t _recipe_id);
};

#endif // RECIPE_PARSER_HPP