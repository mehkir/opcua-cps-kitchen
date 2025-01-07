#include "../include/robot_actions.hpp"

#define PEEL "peel"
#define CUT "cut"
#define BRAISE "braise"
#define MASH "mash"
#define STIR "stir"
#define SPRINKLE "sprinkle"
#define POUR "pour"
#define WHIP "whip"
#define MIX "mix"
#define CRUSH "crush"
#define LAYER "layer"
#define BOIL "boil"
#define BAKE "bake"

#define PEELING_TIME 5
#define CUTTING_TIME 3
#define BRAISING_TIME 8
#define MASHING_TIME 5
#define STIRRING_TIME 3
#define SPRINKLING_TIME 1
#define POURING_TIME 1
#define WHIPPING_TIME 3
#define MIXING_TIME 3
#define CRUSHING_TIME 2
#define LAYERING_TIME 2

robot_actions* robot_actions::instance_;
std::mutex robot_actions::mutex_;

robot_actions* robot_actions::get_instance() {
    std::lock_guard<std::mutex> lockguard(mutex_);
    if(instance_ == nullptr) {
        instance_ = new robot_actions();
    }
    return instance_;
}

robot_actions::robot_actions() {
    // autonomous timed actions
    action_map_[PEEL] = robot_action(PEEL, robot_tools::PEELER, PEELING_TIME);
    action_map_[CUT] = robot_action(CUT, robot_tools::CUTTER, CUTTING_TIME);
    action_map_[BRAISE] = robot_action(BRAISE, robot_tools::PAN, BRAISING_TIME);
    action_map_[MASH] = robot_action(MASH, robot_tools::MASHER, MASHING_TIME);
    action_map_[STIR] = robot_action(STIR, robot_tools::STIRRER, STIRRING_TIME);
    action_map_[SPRINKLE] = robot_action(SPRINKLE, robot_tools::INGREDIENT_DISPENSER, SPRINKLING_TIME);
    action_map_[POUR] = robot_action(POUR, robot_tools::INGREDIENT_DISPENSER, POURING_TIME);
    action_map_[WHIP] = robot_action(WHIP, robot_tools::WHISK, WHIPPING_TIME);
    action_map_[MIX] = robot_action(MIX, robot_tools::MIXER, MIXING_TIME);
    action_map_[CRUSH] = robot_action(CRUSH, robot_tools::CRUSHER, CRUSHING_TIME);
    action_map_[LAYER] = robot_action(LAYER, robot_tools::LAYERING_DISPENSER, LAYERING_TIME);
    // recipe timed actions
    action_map_[BOIL] = robot_action(BOIL, robot_tools::POT);
    action_map_[BAKE] = robot_action(BAKE, robot_tools::OVEN);
}

robot_actions::~robot_actions() {
}

bool robot_actions::has_action(const std::string _action_name) const {
    return action_map_.find(_action_name) != action_map_.end();
}

robot_action robot_actions::get_robot_action(const std::string _action_name) const {
    return action_map_.at(_action_name);
}