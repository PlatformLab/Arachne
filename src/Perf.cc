#include "Common.h"
#include "Arachne.h"
#include "SpinLock.h"
#include "PerfUtils/Cycles.h"

using Arachne::SpinLock;
using PerfUtils::Cycles;

double spinLock() {
    int count = 1000000;
    SpinLock lock("Perf");
    uint64_t start = Cycles::rdtscp();
    for (int i = 0; i < count; i++) {
        lock.lock();
        lock.unlock();
    }
    uint64_t stop = Cycles::rdtscp();
    return Cycles::toSeconds(stop - start)/count;
}
int main(){
    double secs = spinLock();
    if (secs < 1.0e-06) {
        printf("%8.2fns", 1e09*secs);
    } else if (secs < 1.0e-03) {
        printf("%8.2fus", 1e06*secs);
    } else if (secs < 1.0) {
        printf("%8.2fms", 1e03*secs);
    } else {
        printf("%8.2fs", secs);
    }
    putchar('\n');
}

