#ifndef RECIPE_PARSER_HPP
#define RECIPE_PARSER_HPP

#include <queue>

#include "robot_actions.hpp"


struct recipe {
    private:
        recipe_id_t recipe_id_;
        std::string dish_name_;
        std::queue<robot_action> action_queue_;
    public:
        recipe(recipe_id_t _recipe_id, std::string _dish_name, std::queue<robot_action> _action_queue) : recipe_id_(_recipe_id), dish_name_(_dish_name), action_queue_(_action_queue){
        }

        recipe_id_t get_recipe_id() {
            return recipe_id_;
        }

        std::string get_dish_name() {
            return dish_name_;
        }

        std::queue<robot_action> get_action_queue() {
            return action_queue_;
        }

};

class recipe_parser {
    private:
        std::unordered_map<cps_kitchen::recipe_id_t, recipe> recipe_map_;
    public:
        recipe_parser(std::string _recipe_path);
        ~recipe_parser();
        bool has_recipe(const cps_kitchen::recipe_id_t _recipe_id) const;
        recipe get_recipe(cps_kitchen::recipe_id_t _recipe);
};

#endif // RECIPE_PARSER_HPP