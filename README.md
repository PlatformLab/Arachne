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

Under this approach, the kernel needs not preemptively multiplex between
threads without any awareness of what the application is doing, avoiding the
overheads of slow context switches, priority inversion, and cache pollution
from the threads of other processes.


## What is Arachne?

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

1. Clone and build [PerfUtils](https://github.com/PlatformLab/PerfUtils) in a
   directory structure parallel to this one, so the overall directory structure
   looks like the following.

		application_directory/
			Arachne/
			PerfUtils

2. Build the library with `make` in the top-level directory. 

3. Write your application using the public Arachne API, documented [here](https://platformlab.github.io/Arachne/group__api.html).
   The last call in your main function must be Arachne::waitForTermination() to
   ensure proper cleanup on application termination.


		#include <stdio.h>
		#include "Arachne.h"

		void helloWorld() {
			printf("Arachne says hello world and terminates\n");
			Arachne::shutDown();
		}
		int main(int argc, const char** argv){
			Arachne::init(&argc, argv);
			Arachne::createThread(helloWorld);
			Arachne::waitForTermination();
		}


4. Link your application against `-lArachne`.

    g++ -std=c++11 -o MyApp -IArachne MyApp.cc  -LArachne -lArachne -LPerfUtils -lPerfUtils -pthread

## What is on the roadmap?

 - A kernel patch to enable core exclusivity.
 - A kernel patch to handle blocking IO.
 - One or more heuristics for determining when to scale up and scale down the number of cores.
