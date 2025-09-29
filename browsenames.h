#ifndef NODE_IDS_HPP
#define NODE_IDS_HPP

/* ROBOT */
// object type node
#define ROBOT_TYPE "RobotType"
#define REMOTE_ROBOT_TYPE "RemoteRobotType"
// method nodes
#define RECEIVE_TASK "ReceiveTask"
#define HANDOVER_FINISHED_ORDER "HandoverFinishedOrder"
// attribute nodes
#define POSITION "Position"
#define RECIPE_ID "RecipeId"
#define DISH_NAME "DishName"
#define ACTION_NAME "ActionName"
#define INGREDIENTS "Ingredients"
#define OVERALL_TIME "OverallTime"
#define CURRENT_TOOL "CurrentTool"
#define LAST_EQUIPPED_TOOL "LastEquippedTool"
#define CAPABILITIES "Capabilities"
#define PROCESSED_STEPS "ProcessedSteps"
#define PROCESSABLE_STEPS "ProcessableSteps"
#define OVERALL_PROCESSED_STEPS "OverallProcessedSteps"
#define OVERALL_PROCESSING_STEPS "OverallProcessingSteps"

/* CONVEYOR */
// object type node
#define CONVEYOR_TYPE "ConveyorType"
#define REMOTE_CONVEYOR_TYPE "RemoteConveyorType"
#define PLATE_TYPE "PlateType"
// plate attribute nodes
#define PLATE_ID "Id"
#define PLATE_POSITION "Position"
#define PLATE_RECIPE_ID "RecipeId"
#define PLATE_OCCUPIED "Occupied"
// method nodes
#define FINISHED_ORDER_NOTIFICATION "FinishedOrderNotification"
// attribute nodes
#define TOTAL_PLATES "TotalPlates"
#define OCCUPIED_PLATES "OccupiedPlates"

/* CONTROLLER */
// object type node
#define CONTROLLER_TYPE "ControllerType"
#define REMOTE_CONTROLLER_TYPE "RemoteControllerType"
// method nodes
#define REGISTER_ROBOT "RegisterRobot"
#define CHOOSE_NEXT_ROBOT "ChooseNextRobot"
// attribute nodes
#define REGISTERED_ROBOTS "RegisteredRobots"

/* KITCHEN */
// object type node
#define KITCHEN_TYPE "KitchenType"
// method nodes
#define PLACE_RANDOM_ORDER "PlaceRandomOrder"
#define RECEIVE_COMPLETED_ORDER "ReceiveCompletedOrder"
// attribute nodes
#define CONNECTIVITY "Connectivity"
#define RECEIVED_ORDERS "ReceivedOrders"
#define ASSIGNED_ORDERS "AssignedOrders"
#define DROPPED_ORDERS "DroppedOrders"
#define COMPLETED_ORDERS "CompletedOrders"

#endif // NODE_IDS_HPP