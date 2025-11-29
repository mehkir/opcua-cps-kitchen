/**
 * @file time_unit.hpp
 * @brief Millisecond-based timing constants for CPS Kitchen OPC UA agents.
 *
 * @details
 * Defines action durations (in milliseconds) for Robot, Conveyor, and Kitchen agents.
 */
#ifndef TIME_UNIT_HPP
#define TIME_UNIT_HPP

#include <open62541/types.h>

/* Robot-Agent */
#define ROBOT_MOVE_TIME 1000LL
#define RECONFIGURATION_TIME 1000LL
#define RETOOLING_TIME 150LL
#define ACTION_FACTOR 100LL

/* Conveyor-Agent */
#define DEBOUNCE_TIME 500LL
#define CONVEYOR_MOVE_TIME 500LL

/* Kitchen-Agent */
#define PlACING_RATE 50LL

#endif // TIME_UNIT_HPP