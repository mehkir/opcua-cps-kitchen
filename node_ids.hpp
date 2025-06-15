#ifndef NODE_IDS_HPP
#define NODE_IDS_HPP

/* ROBOT */
// method nodes
#define RECEIVE_TASK "receive_task"
#define HANDOVER_FINISHED_ORDER "handover_finished_order"
// information nodes
#define AGENT_TYPE "agent_type"
#define RECIPE_ID "recipe_id"
#define DISH_NAME "dish_name"
#define ACTION_NAME "action_name"
#define INGREDIENTS "ingredients"
#define OVERALL_TIME "overall_time"
#define CURRENT_TOOL "current_tool"
#define LAST_EQUIPPED_TOOL "last_equipped_tool"

/* CONVEYOR */
// method nodes
#define FINISHED_ORDER_NOTIFICATION "finished_order_notification"

/* CONTROLLER */
// method nodes
#define REGISTER_ROBOT "register_robot"
#define CHOOSE_NEXT_ROBOT "choose_next_robot"
#define PLACE_RANDOM_ORDER "place_random_order"

#endif // NODE_IDS_HPP