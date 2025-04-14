#ifndef TIME_UNIT_HPP
#define TIME_UNIT_HPP

#include <open62541/types.h>

// #define TIME_UNIT (UA_DATETIME_MSEC * 100LL)
#define TIME_UNIT UA_DATETIME_SEC
#define TIME_UNIT_UPDATE_RATE 1

#endif // TIME_UNIT_HPP