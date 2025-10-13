#ifndef MAPE_HPP
#define MAPE_HPP

struct remote_robot;

class mape {
private:

protected:
    mape() = default;
public:
    virtual
    ~mape() = default;

    virtual
    remote_robot* on_new_order() = 0;
};

#endif // MAPE_HPP