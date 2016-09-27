#ifndef ARACHNE_CONDITION_H
#define ARACHNE_CONDITION_H

#include <queue>
#include "SpinLock.h"
#include "Arachne.h"


namespace  Arachne {

class ConditionVariable {
    public:
        ConditionVariable();
        ~ConditionVariable();
        void notify_one();
        void notify_all();
        void wait(SpinLock& lock);
        DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
    private:
        std::deque<UserContext*> blockedThreads;
};

}
#endif
