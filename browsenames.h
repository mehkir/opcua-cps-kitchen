#ifndef NODE_IDS_HPP
#define NODE_IDS_HPP

/* ROBOT */
// object type node
#define ROBOT_TYPE "RobotType"
// method nodes
#define RECEIVE_TASK "ReceiveTask"
#define HANDOVER_FINISHED_ORDER "HandoverFinishedOrder"
// information nodes
#define AGENT_TYPE "AgentType"
#define RECIPE_ID "RecipeId"
#define DISH_NAME "DishName"
#define ACTION_NAME "ActionName"
#define INGREDIENTS "Ingredients"
#define OVERALL_TIME "OverallTime"
#define CURRENT_TOOL "CurrentTool"
#define LAST_EQUIPPED_TOOL "LastEquippedTool"
#define CAPABILITIES "Capabilities"

/* CONVEYOR */
// object type node
#define CONVEYOR_TYPE "ConveyorType"
// method nodes
#define FINISHED_ORDER_NOTIFICATION "FinishedOrderNotification"

/* CONTROLLER */
// object type node
#define CONTROLLER_TYPE "ControllerType"
// method nodes
#define REGISTER_ROBOT "RegisterRobot"
#define CHOOSE_NEXT_ROBOT "ChooseNextRobot"
#define PLACE_RANDOM_ORDER "PlaceRandomOrder"

#endif // NODE_IDS_HPP