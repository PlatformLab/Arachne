#ifndef ARACHNE_CONDITION_H
#define ARACHNE_CONDITION_H

#include <queue>
#include "SpinLock.h"
#include "Arachne.h"


namespace  Arachne {
    
// A macro to disallow the copy constructor and operator= functions
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName&) = delete;             \
    TypeName& operator=(const TypeName&) = delete;
#endif

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
