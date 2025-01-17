#include "../include/recipe_parser.hpp"

#include <jsoncpp/json/json.h>
#include <fstream>
#include <stdexcept>
#include <memory>

#include "types.hpp"

#define DISH_NAME_KEY "name"
#define INSTRUCTIONS_KEY "instructions"
#define ACTION_KEY "action"
#define INGREDIENTS_KEY "ingredients"
#define DURATION_KEY "duration"


recipe_parser::recipe_parser(std::string _recipe_path) {
    robot_actions* actions = robot_actions::get_instance();
    std::ifstream ifs_recipe(_recipe_path);
    Json::Value recipes;
    Json::Reader reader;
    reader.parse(ifs_recipe, recipes);
    for (size_t recipe_id = 1; recipe_id <= recipes.size(); recipe_id++) {
        std::string dish_name = recipes[std::to_string(recipe_id)][DISH_NAME_KEY].asString();
        std::queue<robot_action> action_queue;
        duration_t cooking_time = 0;
        duration_t retooling_time = 0;
        for (auto instruction : recipes[std::to_string(recipe_id)][INSTRUCTIONS_KEY]) {
            if (!instruction.isMember(ACTION_KEY)) {
                std::string error_string = "There is a missing action for recipe_id " + std::to_string(recipe_id);
                throw std::invalid_argument(error_string);
            }
            if (!actions->has_action(instruction[ACTION_KEY].asString())) {
                std::string error_string = "There is no entry for the action " + instruction[ACTION_KEY].asString();
                throw std::invalid_argument(error_string);
            }
            std::shared_ptr<action> act = actions->get_robot_action(instruction[ACTION_KEY].asString());
            std::shared_ptr<autonomous_action> autonomous_act = std::dynamic_pointer_cast<autonomous_action>(act);
            std::shared_ptr<recipe_timed_action> recipe_timed_act = std::dynamic_pointer_cast<recipe_timed_action>(act);
            std::string action_name = autonomous_act != nullptr ? autonomous_act->get_name() : recipe_timed_act->get_name();
            if (instruction.isMember(DURATION_KEY) && autonomous_act != nullptr) {
                std::string error_string = "The action " + action_name + " in recipe id " + std::to_string(recipe_id) + " is autonomous and must not contain a duration";
                throw std::invalid_argument(error_string);    
            }
            if (!instruction.isMember(DURATION_KEY) && recipe_timed_act != nullptr) {
                std::string error_string = "The action " + action_name + " in recipe id " + std::to_string(recipe_id) + " is recipe timed and must contain a duration";
                throw std::invalid_argument(error_string);    
            }
            if (!instruction.isMember(INGREDIENTS_KEY)) {
                std::string error_string = "There are no ingredients given for the " + action_name + " action in recipe id " + std::to_string(recipe_id);
                throw std::invalid_argument(error_string);   
            }
            duration_t action_time;
            robot_tool required_tool;
            if (autonomous_act != nullptr) {
                action_time = autonomous_act->get_action_duration();
                required_tool = autonomous_act->get_required_tool();
            } else {
                action_time = instruction[DURATION_KEY].asUInt();
                required_tool = recipe_timed_act->get_required_tool();
            }
            cooking_time += action_time;
            if (!action_queue.empty()) {
                retooling_time += required_tool != action_queue.back().get_required_tool() ? RETOOLING_TIME : 0;
            }
            action_queue.push(robot_action(action_name, required_tool, instruction[INGREDIENTS_KEY].asString(), action_time));
        }
        recipe_map_[recipe_id] = std::make_unique<recipe>(recipe_id, dish_name, action_queue, cooking_time, retooling_time);
    }
}

recipe_parser::~recipe_parser() {
}

bool recipe_parser::has_recipe(const cps_kitchen::recipe_id_t _recipe_id) const {
    return recipe_map_.find(_recipe_id) != recipe_map_.end();
}

recipe recipe_parser::get_recipe(cps_kitchen::recipe_id_t _recipe) {
    return recipe_map_.at(_recipe).operator*();
}