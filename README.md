# Arachne: Towards Core-Aware Scheduling

## What is core aware scheduling?

In today's large-scale data center systems, there are many complex software
components which make a binary trade-off between latency and throughput. They
either overprovision their systems to obtain lower latencies and consequently
waste resources, or oversubscribe their systems and experience very high
latencies due to imbalance between application load and system resources. 

Core-aware scheduling is the notion that we can balance an application's
offered load to a system's available resources by scheduling threads at user
level, and performing coarse-grained core allocation at operating system level.

Under this approach, the kernel no longer preemptively multiplexes between
threads without any awareness of what the application is doing. This enables us
to avoid the performance degradations caused by slow context switches, priority
inversion, and cache pollution from the threads of other processes.

## What is Arachne?

According to Greek mythology, [Arachne](https://en.wikipedia.org/wiki/Arachne)
was a mortal weaver who challenged the goddess Athena to a weaving competition.
Similarly, the Arachne user threading system attempts to challenge the current
dominance of kernel threads in the C++ world.

Arachne is the first step towards core-aware scheduling, allowing an
application to run only as many threads in parallel as cores available to it.

Arachne is a user-level, cooperative threading library written in C++, designed
to improve core utlization and maximize throughput in server applications
without impacting latency. Today, it performs M:N scheduling over kernel
threads and features ~200 ns cross-core thread creations and ~100 ns cross-core
signals on Nehalem X3470.

Today, it is highly performant only as long as threads do not need to block in
the kernel, but this limitation is expected to go away in the next few months.

## How do I use it?
1. Recursively clone Arachne inside your application's directory.

        git clone --recursive git@github.com:PlatformLab/Arachne.git

At this point, your application directory should look like the following.

        application_directory/
            Arachne/

2. Build the library with `make` in the Arachne directory.

        cd Arachne
        make

3. Write your application using the public Arachne API, documented [here](https://platformlab.github.io/Arachne/group__api.html).

        #include <stdio.h>
        #include "Arachne.h"

        void numberPrinter(int n) {
            printf("NumberPrinter says %d\n", n);
        }

        // This is where user code should start running.
        void AppMain(int argc, const char** argv) {
            printf("Arachne says hello world and creates a thread.\n");
            auto tid = Arachne::createThread(numberPrinter, 5);
            Arachne::join(tid);
        }

        // The following bootstrapping code should be copied verbatim into most Arachne
        // applications.
        void AppMainWrapper(int argc, const char** argv) {
            AppMain(argc, argv);
            Arachne::shutDown();
        }
        int main(int argc, const char** argv){
            Arachne::init(&argc, argv);
            Arachne::createThread(&AppMainWrapper, argc, argv);
            Arachne::waitForTermination();
        }

4. Link your application against Arachne.

        g++ -std=c++11 -o MyApp -IArachne MyApp.cc  -LArachne -lArachne -LArachne/PerfUtils -lPerfUtils -pthread

## What is on the roadmap?

 - A core arbiter to enable core exclusivity.
 - One or more heuristics for determining when to scale up and scale down the number of cores.

## User Threading vs Kernel Threadpool

For those who are unfamiliar with the benefits of user threading, it may seem
that a simple kernel thread pool would achieve the same result as a user
threading library. However, tasks running in a kernel thread pool generally
should not block at user level, so they must run to completion without blocking.

[Here](http://stackoverflow.com/questions/41276552/wait-in-forkjoin-pool-java/41277690#41277690)
is an example of a use case that would require manual stack ripping in a thread
pool, but could be implemented as a single function under Arachne.
