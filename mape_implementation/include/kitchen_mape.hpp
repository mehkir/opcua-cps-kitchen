#ifndef KITCHEN_MAPE_HPP
#define KITCHEN_MAPE_HPP

#include "mape.hpp"

class kitchen_mape : public mape {
private:

public:
    kitchen_mape();
    ~kitchen_mape();
    virtual remote_robot* on_new_order();
};

#endif // KITCHEN_MAPE_HPP