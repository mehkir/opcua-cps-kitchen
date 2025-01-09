#include "../include/recipe_parser.hpp"

#include <jsoncpp/json/json.h>
#include <fstream>
#include <stdexcept>
#include <memory>

#include "types.hpp"

#define DISH_NAME_KEY "name"
#define ACTION_KEY "action"
#define DURATION_KEY "duration"
#define INSTRUCTIONS_KEY "instructions"

recipe_parser::recipe_parser(std::string _recipe_path) {
    robot_actions* actions = robot_actions::get_instance();
    std::ifstream ifs_recipe(_recipe_path);
    Json::Value recipes;
    Json::Reader reader;
    reader.parse(ifs_recipe, recipes);
    for (size_t recipe_id = 1; recipe_id <= recipes.size(); recipe_id++) {
        std::string dish_name = recipes[std::to_string(recipe_id)][DISH_NAME_KEY].asString();
        std::queue<robot_action> action_queue;
        std::unique_ptr<robot_action> robot_action = nullptr;
        // instructions extraction
        for (auto instruction : recipes[std::to_string(recipe_id)][INSTRUCTIONS_KEY]) {
            if (!instruction.isMember(ACTION_KEY)) {
                std::string error_string = "There is a missing action for recipe_id " + std::to_string(recipe_id);
                throw std::invalid_argument(error_string);
            }
            std::shared_ptr<action> actionr = actions->get_robot_action(instruction[ACTION_KEY].asString());
            if (instruction.isMember(DURATION_KEY)) {
                std::shared_ptr<recipe_timed_action> recipe_timed_act = std::dynamic_pointer_cast<recipe_timed_action>(actionr);
                if(true) {
                    // std::string error_string = "The action " + action.get_name() + " in recipe id " + std::to_string(recipe_id) + " must not contain a duration for an autonomous action";
                    // throw std::invalid_argument(error_string);
                }
                duration_t action_time = instruction[DURATION_KEY].asUInt();

                // action = robot_action(action.get_name(), action.get_required_tool(), action_time, action.is_recipe_timed());
            }
        }
        // recipe_map_[recipe_id] = recipe(recipe_id, dish_name)
    }
}

recipe_parser::~recipe_parser() {
}

bool recipe_parser::has_recipe(const cps_kitchen::recipe_id_t _recipe_id) const {
    return recipe_map_.find(_recipe_id) != recipe_map_.end();
}

recipe recipe_parser::get_recipe(cps_kitchen::recipe_id_t _recipe) {
    return recipe_map_.at(_recipe);
}