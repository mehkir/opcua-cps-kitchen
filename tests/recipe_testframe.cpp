#include "recipe_parser.hpp"
#include <iostream>

int main (int argc, char* argv[]) {
    recipe_parser rp;
    if(rp.has_recipe(1)) {
        recipe rcp = rp.get_recipe(3);
        std::cout << "Recipe ID: " << rcp.get_recipe_id() << std::endl;
        std::cout << "Dish name: " << rcp.get_dish_name() << std::endl;
        std::queue<robot_action> action_queue = rcp.get_action_queue();
        while (action_queue.size()) {
            robot_action robot_act = action_queue.front();
            action_queue.pop();
            std::cout << "Action name: " << robot_act.get_name() << std::endl;
            std::cout << "Required tool: " << robot_tool_to_string(robot_act.get_required_tool()) << std::endl;
            std::cout << "Ingredients: " << robot_act.get_ingredients() << std::endl;
            std::cout << "Duration: " << robot_act.get_action_duration() << std::endl;
        }
        std::cout << "Cooking time: " << rcp.get_cooking_time() << std::endl;
        std::cout << "Retooling time: " << rcp.get_retooling_time() << std::endl;
        std::cout << "Overall time: " << rcp.get_overall_time() << std::endl;
    }
    return 0;
}