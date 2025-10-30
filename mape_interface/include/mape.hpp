#ifndef MAPE_HPP
#define MAPE_HPP

#include <map>
#include <queue>
#include <memory>
#include <functional>
#include "types.hpp"
#include "robot_actions.hpp"

using namespace cps_kitchen;

struct remote_robot;

typedef std::function<void(position_t, position_t)> swap_robot_positions_callback_t; /**< the callback declaration to swap robot positions pair-wise. */
typedef std::function<void(position_t, std::string)> reconfigure_robot_callback_t; /**< the callback declaration to reconfigure a robot. */

class mape {
private:

protected:
    mape() = default;
    swap_robot_positions_callback_t swap_robot_positions_callback_;
    reconfigure_robot_callback_t reconfigure_robot_callback_;
public:
    virtual ~mape() = default;

    /**
     * @brief Callback when new order is placed.
     * 
     * @param _position_remote_robot_map the position remote robot map by the controller.
     * @param _recipe_action_queue the action queue with remaining steps to perform on the order.
     * @return remote_robot* the remote robot for the next order steps.
     */
    virtual remote_robot* on_new_order(const std::map<position_t, std::unique_ptr<remote_robot>, std::greater<position_t>>& _position_remote_robot_map, std::queue<robot_action> _recipe_action_queue) = 0;

    /**
     * @brief Sets the swap robot positions callback.
     * 
     * @param _swap_robot_positions_callback the swap robot position callback.
     */
    virtual void set_swap_robot_positions_callback(swap_robot_positions_callback_t _swap_robot_positions_callback) {
        swap_robot_positions_callback_ = _swap_robot_positions_callback;
    }

    /**
     * @brief Sets the reconfigure robot callback.
     * 
     * @param _reconfigure_robot_callback the reconfigure robot callback.
     */
    virtual void set_reconfigure_robot_callback(reconfigure_robot_callback_t _reconfigure_robot_callback) {
        reconfigure_robot_callback_ = _reconfigure_robot_callback;
    }
};

#endif // MAPE_HPP