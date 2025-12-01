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
#define ROBOT_MOVE_TIME 100LL
#define RECONFIGURATION_TIME 100LL
#define RETOOLING_TIME 50LL
#define ACTION_FACTOR 10LL

/* Conveyor-Agent */
#define DEBOUNCE_TIME 100LL
#define CONVEYOR_MOVE_TIME 100LL

#endif // TIME_UNIT_HPP