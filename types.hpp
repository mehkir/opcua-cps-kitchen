#ifndef TYPES_HPP
#define TYPES_HPP

#include <open62541/types.h>

namespace cps_kitchen {
    typedef UA_UInt16 port_t;
    typedef UA_UInt32 position_t;
    typedef UA_UInt32 plate_id_t;
    typedef UA_UInt32 steps_t;
    typedef UA_UInt32 recipe_id_t;
    typedef UA_UInt64 duration_t;
};
#endif // TYPES_HPP