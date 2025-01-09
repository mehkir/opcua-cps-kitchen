#ifndef RECIPE_PARSER_HPP
#define RECIPE_PARSER_HPP

#include <queue>

#include "robot_actions.hpp"


struct recipe {
    private:
        recipe_id_t recipe_id_;
        std::string dish_name_;
        std::queue<robot_action> action_queue_;
        duration_t cooking_time_;
        duration_t retooling_time_;
    public:
        recipe(recipe_id_t _recipe_id, std::string _dish_name, std::queue<robot_action> _action_queue, duration_t _cooking_time, duration_t _retooling_time) : recipe_id_(_recipe_id), dish_name_(_dish_name), action_queue_(_action_queue), cooking_time_(_cooking_time), retooling_time_(_retooling_time){
        }

        recipe_id_t get_recipe_id() const {
            return recipe_id_;
        }

        std::string get_dish_name() const {
            return dish_name_;
        }

        std::queue<robot_action> get_action_queue() const {
            return action_queue_;
        }

        duration_t get_cooking_time() const {
            return cooking_time_;
        }

        duration_t get_retooling_time() const {
            return retooling_time_;
        }

        duration_t get_overall_time() const {
            return cooking_time_ + retooling_time_;
        }

};

class recipe_parser {
    private:
        std::unordered_map<cps_kitchen::recipe_id_t, std::unique_ptr<recipe>> recipe_map_;
    public:
        recipe_parser(std::string _recipe_path);
        ~recipe_parser();
        bool has_recipe(const cps_kitchen::recipe_id_t _recipe_id) const;
        recipe get_recipe(cps_kitchen::recipe_id_t _recipe);
};

#endif // RECIPE_PARSER_HPP