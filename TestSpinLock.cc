#include <stdio.h>
#include "SpinLock.h"
#include "Cycles.h"

using Arachne::SpinLock;
using PerfUtils::Cycles;
// Measure the cost of acquiring and releasing a SpinLock (assuming the
// lock is initially free).
double spinLock()
{
    int count = 1000000;
    SpinLock lock;
    uint64_t start = Cycles::rdtsc();
    for (int i = 0; i < count; i++) {
        lock.lock();
        lock.unlock();
    }
    uint64_t stop = Cycles::rdtsc();
    return Cycles::toSeconds(stop - start)/count;
}
int main(){
    double result = spinLock();
    printf("%fns\n", 1e09*result);
    return 0;
}
