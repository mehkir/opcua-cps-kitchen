#ifndef MAPE_HPP
#define MAPE_HPP

#include <map>
#include <unordered_map>
#include <cstdio>
#include <queue>
#include <memory>
#include <functional>
#include <filesystem>
#include <limits.h>
#include <unistd.h>
#include "types.hpp"
#include "robot_actions.hpp"
#include "capability_parser.hpp"

using namespace cps_kitchen;

struct remote_robot;

typedef std::function<void(position_t, position_t)> swap_robot_positions_callback_t; /**< the callback declaration to swap robot positions pair-wise. */
typedef std::function<void(position_t, std::string)> reconfigure_robot_callback_t; /**< the callback declaration to reconfigure a robot. */

class mape {
private:
    std::unordered_map<std::string, capability_parser> capabilites_map_; /**< the capabilities map holding all available profiles. */

protected:
    mape() {
        char buffer[PATH_MAX + 1];  // +1 for the null terminator
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len == -1) {
            perror("readlink");
            return;
        }
        buffer[len] = '\0';  // null terminate
        std::filesystem::path exe_path(buffer);
        std::filesystem::path exe_dir = exe_path.parent_path();
        std::filesystem::path capabilities_dir = exe_dir.parent_path() / "capabilities";
        // Iterate JSON files in capabilites direcotry
        try {
            for (const auto& entry : std::filesystem::directory_iterator(capabilities_dir)) {
                if (!entry.is_regular_file()) continue;
                const auto& p = entry.path();
                if (p.has_extension() && p.extension() == ".json") {
                    capabilites_map_.emplace(p.string(), capability_parser(p.string()));
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::fprintf(stderr, "mape: error iterating capabilities dir: %s\n", e.what());
        }
    }

    /**
     * @brief Returns available capability profiles.
     * 
     * @return std::unordered_map<std::string, capability_parser> the capabilities map.
     */
    virtual std::unordered_map<std::string, capability_parser> get_capabilites() const {
        return capabilites_map_;
    }

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