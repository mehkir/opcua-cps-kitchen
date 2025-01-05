#ifndef CAPABILITY_ACTIONS.HPP
#define CAPABILITY_ACTIONS.HPP

#include <mutex>

class capability_actions {
    public:
        static capability_actions* get_instance();
    private:
        capability_actions();
        ~capability_actions();
        static capability_actions* instance_;
        static std::mutex mutex_;
};

#endif // CAPABILITY_ACTIONS