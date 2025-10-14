#ifndef MAPE_HPP
#define MAPE_HPP

#include <map>
#include <queue>
#include <memory>
#include "types.hpp"
#include "robot_actions.hpp"

using namespace cps_kitchen;

struct remote_robot;

class mape {
private:

protected:
    mape() = default;
public:
    virtual ~mape() = default;

    virtual remote_robot* on_new_order(std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) = 0;
};

#endif // MAPE_HPP