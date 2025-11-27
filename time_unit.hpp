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
#define MOVE_TIME 5LL
#define RECONFIGURATION_TIME 5LL
#define RETOOLING_TIME 1LL

/* Conveyor-Agent */
#define DEBOUNCE_TIME 100LL
#define MOVE_TIME 100LL

/* Kitchen-Agent */
#define PlACING_RATE 5LL

#endif // TIME_UNIT_HPP