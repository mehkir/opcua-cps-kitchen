#ifndef MAPE_HPP
#define MAPE_HPP

class mape {
private:

protected:
    mape() = default;
public:
    virtual
    ~mape() = default;

    virtual
    double on_new_order() = 0;
};

#endif // MAPE_HPP