#ifndef SESSION_ID_HPP
#define SESSION_ID_HPP

#include <open62541/types.h>

typedef UA_UInt32 session_id_t;
typedef UA_UInt32 message_counter_t;

struct session_id {
    private:
        session_id_t id_;
        message_counter_t message_counter_;
    public:
        session_id(session_id_t _id, message_counter_t _message_counter) : id_(_id), message_counter_(_message_counter) {
        }

        ~session_id() {
        }

        friend bool operator< (const session_id& _session_id_lh, const session_id& _session_id_rh) {
            if (_session_id_lh.id_ < _session_id_rh.id_)
                return true;
            if (_session_id_lh.id_ == _session_id_rh.id_ && _session_id_lh.message_counter_ < _session_id_rh.message_counter_)
                return true;
            return false;
        }

        bool operator<= (const session_id& _session_id) const {
            return !(_session_id < *this);
        }
        
        bool operator> (const session_id& _session_id) const {
            return _session_id < *this;
        }
        
        bool operator>= (const session_id& _session_id) const {
            return !(*this < _session_id);
        }

        bool operator== (const session_id& _session_id) const {
            return id_ == _session_id.id_ && message_counter_ == _session_id.message_counter_;
        }
};

#endif // SESSION_ID_HPP