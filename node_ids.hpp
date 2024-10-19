#ifndef NODE_IDS_HPP
#define NODE_IDS_HPP

/* CLOCK */
#define CLOCK_TICK "clock_tick"
#define RECEIVE_TICK_ACK "receive_tick_ack"

/* ROBOT */
#define RECEIVE_TASK "receive_task"

/* CONVEYOR */
#define RECEIVE_MOVE_INSTRUCTION "receive_move_instruction"
#define PLACE_FINISHED_ORDER "place_finished_order"

/* CONTROLLER */
#define RECEIVE_ROBOT_STATE "receive_robot_state"
#define RECEIVE_CONVEYOR_STATE "receive_conveyor_state"
#define RECEIVE_PROCEEDED_TO_NEXT_TICK_NOTIFICATION "receive_proceeded_to_next_tick_notification"

#endif // NODE_IDS_HPP