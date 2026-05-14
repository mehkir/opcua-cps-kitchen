#include "../include/robot_actions.hpp"
#include "../include/robot_action_names.hpp"

#include "timing_config.hpp"

robot_actions* robot_actions::instance_ = nullptr;
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
    action_map_[PEEL] = std::make_shared<autonomous_action>(PEEL, robot_tool::PEELER, timing_config::get_instance()->get_timing(ROBOT_TIMES, PEEL));
    action_map_[CUT] = std::make_shared<autonomous_action>(CUT, robot_tool::CUTTER, timing_config::get_instance()->get_timing(ROBOT_TIMES, CUT));
    action_map_[BRAISE] = std::make_shared<autonomous_action>(BRAISE, robot_tool::PAN, timing_config::get_instance()->get_timing(ROBOT_TIMES, BRAISE));
    action_map_[MASH] = std::make_shared<autonomous_action>(MASH, robot_tool::MASHER, timing_config::get_instance()->get_timing(ROBOT_TIMES, MASH));
    action_map_[STIR] = std::make_shared<autonomous_action>(STIR, robot_tool::STIRRER, timing_config::get_instance()->get_timing(ROBOT_TIMES, STIR));
    action_map_[SPRINKLE] = std::make_shared<autonomous_action>(SPRINKLE, robot_tool::INGREDIENT_DISPENSER, timing_config::get_instance()->get_timing(ROBOT_TIMES, SPRINKLE));
    action_map_[POUR] = std::make_shared<autonomous_action>(POUR, robot_tool::INGREDIENT_DISPENSER, timing_config::get_instance()->get_timing(ROBOT_TIMES, POUR));
    action_map_[WHIP] = std::make_shared<autonomous_action>(WHIP, robot_tool::WHISK, timing_config::get_instance()->get_timing(ROBOT_TIMES, WHIP));
    action_map_[MIX] = std::make_shared<autonomous_action>(MIX, robot_tool::MIXER, timing_config::get_instance()->get_timing(ROBOT_TIMES, MIX));
    action_map_[CRUSH] = std::make_shared<autonomous_action>(CRUSH, robot_tool::CRUSHER, timing_config::get_instance()->get_timing(ROBOT_TIMES, CRUSH));
    action_map_[LAYER] = std::make_shared<autonomous_action>(LAYER, robot_tool::LAYERING_DISPENSER, timing_config::get_instance()->get_timing(ROBOT_TIMES, LAYER));
    action_map_[FRY] = std::make_shared<autonomous_action>(FRY, robot_tool::FRYER, timing_config::get_instance()->get_timing(ROBOT_TIMES, FRY));
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