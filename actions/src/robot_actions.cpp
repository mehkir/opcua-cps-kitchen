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
#define FRY "fry"

#define PEELING_TIME 5LL
#define CUTTING_TIME 3LL
#define BRAISING_TIME 8LL
#define MASHING_TIME 5LL
#define STIRRING_TIME 3LL
#define SPRINKLING_TIME 1LL
#define POURING_TIME 1LL
#define WHIPPING_TIME 3LL
#define MIXING_TIME 3LL
#define CRUSHING_TIME 2LL
#define LAYERING_TIME 2LL
#define FRYING_TIME 3LL

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
    action_map_[PEEL] = std::make_shared<autonomous_action>(PEEL, robot_tool::PEELER, PEELING_TIME);
    action_map_[CUT] = std::make_shared<autonomous_action>(CUT, robot_tool::CUTTER, CUTTING_TIME);
    action_map_[BRAISE] = std::make_shared<autonomous_action>(BRAISE, robot_tool::PAN, BRAISING_TIME);
    action_map_[MASH] = std::make_shared<autonomous_action>(MASH, robot_tool::MASHER, MASHING_TIME);
    action_map_[STIR] = std::make_shared<autonomous_action>(STIR, robot_tool::STIRRER, STIRRING_TIME);
    action_map_[SPRINKLE] = std::make_shared<autonomous_action>(SPRINKLE, robot_tool::INGREDIENT_DISPENSER, SPRINKLING_TIME);
    action_map_[POUR] = std::make_shared<autonomous_action>(POUR, robot_tool::INGREDIENT_DISPENSER, POURING_TIME);
    action_map_[WHIP] = std::make_shared<autonomous_action>(WHIP, robot_tool::WHISK, WHIPPING_TIME);
    action_map_[MIX] = std::make_shared<autonomous_action>(MIX, robot_tool::MIXER, MIXING_TIME);
    action_map_[CRUSH] = std::make_shared<autonomous_action>(CRUSH, robot_tool::CRUSHER, CRUSHING_TIME);
    action_map_[LAYER] = std::make_shared<autonomous_action>(LAYER, robot_tool::LAYERING_DISPENSER, LAYERING_TIME);
    action_map_[FRY] = std::make_shared<autonomous_action>(FRY, robot_tool::FRYER, FRYING_TIME);
    // recipe timed actions
    action_map_[BOIL] = std::make_shared<recipe_timed_action>(BOIL, robot_tool::POT);
    action_map_[BAKE] = std::make_shared<recipe_timed_action>(BAKE, robot_tool::OVEN);
}

robot_actions::~robot_actions() {
}

bool robot_actions::has_action(const std::string _action_name) const {
    return action_map_.find(_action_name) != action_map_.end();
}

std::shared_ptr<action> robot_actions::get_robot_action(const std::string _action_name) {
    return action_map_.at(_action_name);
}