/**
 * @file agent_timing.hpp
 * @brief Millisecond-based timing constants for CPS Kitchen OPC UA agents.
 *
 * @details
 * Defines action durations (in milliseconds) for Robot, Conveyor, and Kitchen agents.
 */
#ifndef TIME_UNIT_HPP
#define TIME_UNIT_HPP

#include <open62541/types.h>

/* Robot-Agent */
#define ROBOT_MOVE_TIME 100LL /**< Time in milliseconds for the robot to move one position on the conveyor belt. */
#define RECONFIGURATION_TIME 100LL /**< Time in milliseconds for the robot to reconfigure itself to a new capabilities profile. */
#define RETOOLING_TIME 50LL /**< Time in milliseconds for the robot to retool itself. */
#define ACTION_FACTOR 10LL /**< Factor for scaling action durations. */

/* Conveyor-Agent */
#define DEBOUNCE_TIME 100LL /**< Wait time in milliseconds while idling to group closely spaced finished-order notifications before moving. */
#define CONVEYOR_MOVE_TIME 100LL /**< Time in milliseconds for the conveyor to move one position. */

#endif // TIME_UNIT_HPP