#ifndef ARACHNE_CONDITION_H
#define ARACHNE_CONDITION_H

#include <queue>
#include "SpinLock.h"
#include "Arachne.h"


namespace  Arachne {

class condition_variable {
    public:
        condition_variable();
        ~condition_variable();
        void notify_one();
        void notify_all();
        void wait(SpinLock& lock);
        DISALLOW_COPY_AND_ASSIGN(condition_variable);
    private:
        // TODO: This may become core-specific.
        std::deque<UserContext*> blockedThreads;
};

}
#endif
