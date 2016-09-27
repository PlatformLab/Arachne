#ifndef ARACHNE_CONDITION_H
#define ARACHNE_CONDITION_H

#include <queue>
#include "SpinLock.h"
#include "Arachne.h"


namespace  Arachne {

/**
  * This class implements a subset of the functionality of
  * std::condition_variable.
  * It takes no internal locks, so is assumed that notifications are done with
  * the associated mutex held.
  */
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
