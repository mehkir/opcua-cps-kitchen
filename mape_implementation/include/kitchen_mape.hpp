#ifndef KITCHEN_MAPE_HPP
#define KITCHEN_MAPE_HPP

#include "mape.hpp"

class kitchen_mape : public mape {
private:

public:
    using mape::mape;
    ~kitchen_mape() override = default;
    virtual remote_robot* on_new_order(const std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) override;
};

#endif // KITCHEN_MAPE_HPP